package main

import (
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	defaultDownloadThreads     = 32
	maxDownloadThreads         = 64
	downloadProgressTick       = 120 * time.Millisecond
	downloadBufferSizeBytes    = 2 * 1024 * 1024
	minRangedChunkSizeBytes    = 256 * 1024
	maxRangedSegmentSizeBytes  = 64 * 1024 * 1024
	downloadChunkAttempts      = 4
	downloadRetryBaseDelay     = 180 * time.Millisecond
	downloadProbeHeaderTimeout = 20 * time.Second
)

var defaultDownloadHTTPClient = &http.Client{
	Transport: &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   12 * time.Second,
			KeepAlive: 30 * time.Second,
		}).DialContext,
		ForceAttemptHTTP2:     false,
		MaxIdleConns:          maxDownloadThreads * 4,
		MaxIdleConnsPerHost:   maxDownloadThreads * 2,
		MaxConnsPerHost:       maxDownloadThreads * 2,
		IdleConnTimeout:       90 * time.Second,
		TLSHandshakeTimeout:   12 * time.Second,
		ResponseHeaderTimeout: downloadProbeHeaderTimeout,
		ExpectContinueTimeout: time.Second,
	},
}

// DownloadProgressUpdate is emitted by Downloader while bytes are written.
type DownloadProgressUpdate struct {
	Downloaded int64
	Total      int64
	Speed      int64
	Percent    int
	Threads    int
	Done       bool
}

// Downloader handles single and ranged multi-threaded downloads.
type Downloader struct {
	URL        string
	DestPath   string
	NumThreads int
	UserAgent  string
	HTTPClient *http.Client
	OnProgress func(DownloadProgressUpdate)
}

func NewDownloader(url, dest string, threads int, ua string) *Downloader {
	if threads < 1 {
		threads = defaultDownloadThreads
	}
	if threads > maxDownloadThreads {
		threads = maxDownloadThreads
	}
	return &Downloader{
		URL:        url,
		DestPath:   dest,
		NumThreads: threads,
		UserAgent:  ua,
	}
}

func (d *Downloader) Start() error {
	return d.StartContext(context.Background())
}

func (d *Downloader) StartContext(ctx context.Context) error {
	LogDebug("Starting download: %s -> %s (Threads: %d)", d.URL, d.DestPath, d.NumThreads)
	if err := ctx.Err(); err != nil {
		return err
	}
	if strings.TrimSpace(d.URL) == "" {
		return fmt.Errorf("download URL is empty")
	}
	if strings.TrimSpace(d.DestPath) == "" {
		return fmt.Errorf("download destination is empty")
	}
	if err := os.MkdirAll(filepath.Dir(d.DestPath), 0755); err != nil {
		return err
	}

	probe, err := d.probe()
	if err != nil {
		LogDebug("HEAD probe failed, trying ranged GET probe: %v", err)
		probe, err = d.probeRangeMetadata()
		if err != nil {
			LogDebug("Download probe failed, falling back to single-threaded download: %v", err)
			return d.downloadSingle(ctx)
		}
	}
	if probe.contentLength <= 0 {
		LogDebug("Content-Length unknown, falling back to single-threaded download")
		return d.downloadSingle(ctx)
	}
	if !probe.acceptsRanges {
		LogDebug("Range download unsupported or disabled, falling back to single-threaded download")
		return d.downloadSingle(ctx)
	}

	requestedThreads := effectiveThreadCount(d.NumThreads, probe.contentLength)
	segments := buildDownloadSegments(probe.contentLength, requestedThreads)
	threads := requestedThreads
	if threads > len(segments) {
		threads = len(segments)
	}
	if threads <= 1 || len(segments) <= 1 {
		LogDebug("Package too small for ranged download, falling back to single-threaded download")
		return d.downloadSingle(ctx)
	}

	return d.downloadRanged(ctx, probe.contentLength, threads, segments)
}

func effectiveThreadCount(requested int, contentLength int64) int {
	if requested < 1 {
		requested = defaultDownloadThreads
	}
	if requested > maxDownloadThreads {
		requested = maxDownloadThreads
	}
	if contentLength > 0 {
		maxUseful := int((contentLength + minRangedChunkSizeBytes - 1) / minRangedChunkSizeBytes)
		if maxUseful < 1 {
			maxUseful = 1
		}
		if requested > maxUseful {
			requested = maxUseful
		}
	}
	if requested < 1 {
		requested = 1
	}
	return requested
}

type downloadProbeResult struct {
	contentLength int64
	acceptsRanges bool
}

type downloadSegment struct {
	start int64
	end   int64
}

func (d *Downloader) probe() (downloadProbeResult, error) {
	req, err := d.newRequest(http.MethodHead, "", 0, 0)
	if err != nil {
		return downloadProbeResult{}, err
	}

	resp, err := d.do(req)
	if err != nil {
		return downloadProbeResult{}, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return downloadProbeResult{}, fmt.Errorf("HEAD returned status %d", resp.StatusCode)
	}

	contentLength := resp.ContentLength
	if contentLength <= 0 {
		contentLength = parseContentLength(resp.Header.Get("Content-Length"))
	}
	acceptsRanges := strings.EqualFold(strings.TrimSpace(resp.Header.Get("Accept-Ranges")), "bytes")
	if contentLength > 0 && !acceptsRanges {
		ok, err := d.probeRangeSupport(contentLength)
		if err != nil {
			LogDebug("Range probe failed: %v", err)
		}
		acceptsRanges = ok
	}

	return downloadProbeResult{
		contentLength: contentLength,
		acceptsRanges: acceptsRanges,
	}, nil
}

func (d *Downloader) probeRangeSupport(contentLength int64) (bool, error) {
	req, err := d.newRequest(http.MethodGet, "bytes=0-0", 0, 0)
	if err != nil {
		return false, err
	}

	resp, err := d.do(req)
	if err != nil {
		return false, err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)

	if resp.StatusCode != http.StatusPartialContent {
		return false, nil
	}
	if resp.ContentLength == 1 {
		return true, nil
	}
	contentRange := strings.TrimSpace(resp.Header.Get("Content-Range"))
	return strings.HasPrefix(contentRange, "bytes 0-0/") || strings.HasSuffix(contentRange, "/"+strconv.FormatInt(contentLength, 10)), nil
}

func (d *Downloader) probeRangeMetadata() (downloadProbeResult, error) {
	req, err := d.newRequest(http.MethodGet, "bytes=0-0", 0, 0)
	if err != nil {
		return downloadProbeResult{}, err
	}

	resp, err := d.do(req)
	if err != nil {
		return downloadProbeResult{}, err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)

	if resp.StatusCode != http.StatusPartialContent {
		return downloadProbeResult{}, fmt.Errorf("range metadata returned status %d", resp.StatusCode)
	}
	contentLength := parseTotalFromContentRange(resp.Header.Get("Content-Range"))
	if contentLength <= 0 {
		return downloadProbeResult{}, fmt.Errorf("range metadata missing total content length")
	}
	return downloadProbeResult{
		contentLength: contentLength,
		acceptsRanges: true,
	}, nil
}

func parseContentLength(value string) int64 {
	n, err := strconv.ParseInt(strings.TrimSpace(value), 10, 64)
	if err != nil || n <= 0 {
		return -1
	}
	return n
}

func parseTotalFromContentRange(value string) int64 {
	_, total, ok := strings.Cut(strings.TrimSpace(value), "/")
	if !ok || total == "*" {
		return -1
	}
	return parseContentLength(total)
}

func buildDownloadSegments(contentLength int64, requestedThreads int) []downloadSegment {
	threads := effectiveThreadCount(requestedThreads, contentLength)
	if threads <= 1 || contentLength <= minRangedChunkSizeBytes {
		return []downloadSegment{{start: 0, end: contentLength - 1}}
	}

	chunkSize := (contentLength + int64(threads) - 1) / int64(threads)
	if chunkSize > maxRangedSegmentSizeBytes {
		chunkSize = maxRangedSegmentSizeBytes
	}
	if chunkSize < minRangedChunkSizeBytes {
		chunkSize = minRangedChunkSizeBytes
	}

	segments := make([]downloadSegment, 0, int((contentLength+chunkSize-1)/chunkSize))
	for start := int64(0); start < contentLength; start += chunkSize {
		end := start + chunkSize - 1
		if end >= contentLength {
			end = contentLength - 1
		}
		segments = append(segments, downloadSegment{start: start, end: end})
	}
	return segments
}

func (d *Downloader) downloadRanged(parentCtx context.Context, contentLength int64, threads int, segments []downloadSegment) error {
	tmpPath := d.DestPath + ".part"
	_ = os.Remove(tmpPath)

	file, err := os.Create(tmpPath)
	if err != nil {
		return err
	}

	if err := file.Truncate(contentLength); err != nil {
		file.Close()
		_ = os.Remove(tmpPath)
		return err
	}

	progress := newDownloadProgressReporter(contentLength, threads, d.OnProgress)
	progress.emit(false)

	var wg sync.WaitGroup
	segmentChan := make(chan downloadSegment)
	errChan := make(chan error, len(segments))
	ctx, cancel := context.WithCancel(parentCtx)
	defer cancel()

	for i := 0; i < threads; i++ {
		wg.Add(1)
		go func(workerID int) {
			defer wg.Done()
			buf := make([]byte, downloadBufferSizeBytes)
			for segment := range segmentChan {
				if ctx.Err() != nil {
					return
				}
				if err := d.downloadChunk(ctx, file, segment.start, segment.end, buf, progress.add); err != nil {
					select {
					case errChan <- fmt.Errorf("worker %d segment %d-%d: %w", workerID, segment.start, segment.end, err):
					default:
					}
					cancel()
					return
				}
			}
		}(i)
	}

sendSegments:
	for _, segment := range segments {
		select {
		case <-ctx.Done():
			break sendSegments
		case segmentChan <- segment:
		}
	}
	close(segmentChan)

	wg.Wait()
	close(errChan)

	if len(errChan) > 0 {
		file.Close()
		_ = os.Remove(tmpPath)
		return <-errChan
	}
	if err := ctx.Err(); err != nil {
		file.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := file.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	progress.finish()

	if err := replaceDownloadedFile(tmpPath, d.DestPath); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}

	LogDebug("Download completed successfully.")
	return nil
}

func (d *Downloader) downloadSingle(ctx context.Context) error {
	tmpPath := d.DestPath + ".part"
	_ = os.Remove(tmpPath)

	req, err := d.newRequest(http.MethodGet, "", 0, 0)
	if err != nil {
		return err
	}
	req = req.WithContext(ctx)
	resp, err := d.do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("server returned status: %d", resp.StatusCode)
	}

	file, err := os.Create(tmpPath)
	if err != nil {
		return err
	}

	progress := newDownloadProgressReporter(resp.ContentLength, 1, d.OnProgress)
	progress.emit(false)
	if err := copyWithProgress(ctx, file, resp.Body, progress.add); err != nil {
		file.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := ctx.Err(); err != nil {
		file.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := file.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	progress.finish()

	if err := replaceDownloadedFile(tmpPath, d.DestPath); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}
	return nil
}

func (d *Downloader) downloadChunk(ctx context.Context, file *os.File, start, end int64, buf []byte, onBytes func(int64)) error {
	offset := start
	remaining := end - start + 1
	var lastErr error

	for attempt := 1; remaining > 0 && attempt <= downloadChunkAttempts; attempt++ {
		if err := ctx.Err(); err != nil {
			return err
		}

		rangeHeader := fmt.Sprintf("bytes=%d-%d", offset, end)
		req, err := d.newRequest(http.MethodGet, rangeHeader, offset, end)
		if err != nil {
			return err
		}
		req = req.WithContext(ctx)

		resp, err := d.do(req)
		if err != nil {
			lastErr = err
			if !sleepBeforeDownloadRetry(ctx, attempt) {
				return ctx.Err()
			}
			continue
		}

		err = d.copyChunkResponse(file, resp, rangeHeader, buf, &offset, &remaining, onBytes)
		resp.Body.Close()
		if err == nil {
			return nil
		}
		lastErr = err
		if remaining <= 0 {
			return nil
		}
		LogDebug("Retrying range %d-%d after attempt %d/%d failed: %v", offset, end, attempt, downloadChunkAttempts, err)
		if !sleepBeforeDownloadRetry(ctx, attempt) {
			return ctx.Err()
		}
	}
	if remaining != 0 {
		if lastErr != nil {
			return lastErr
		}
		return io.ErrUnexpectedEOF
	}
	return nil
}

func (d *Downloader) copyChunkResponse(file *os.File, resp *http.Response, rangeHeader string, buf []byte, offset *int64, remaining *int64, onBytes func(int64)) error {
	if resp.StatusCode != http.StatusPartialContent {
		return fmt.Errorf("server returned status: %d for range %s", resp.StatusCode, rangeHeader)
	}

	for *remaining > 0 {
		readSize := len(buf)
		if int64(readSize) > *remaining {
			readSize = int(*remaining)
		}
		n, err := resp.Body.Read(buf[:readSize])
		if n > 0 {
			if _, wErr := file.WriteAt(buf[:n], *offset); wErr != nil {
				return wErr
			}
			*offset += int64(n)
			*remaining -= int64(n)
			if onBytes != nil {
				onBytes(int64(n))
			}
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
	}
	if *remaining != 0 {
		return io.ErrUnexpectedEOF
	}
	return nil
}

func sleepBeforeDownloadRetry(ctx context.Context, attempt int) bool {
	delay := downloadRetryBaseDelay * time.Duration(1<<maxInt(attempt-1, 0))
	timer := time.NewTimer(delay)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return false
	case <-timer.C:
		return true
	}
}

func (d *Downloader) newRequest(method string, rangeHeader string, rangeStart int64, rangeEnd int64) (*http.Request, error) {
	req, err := http.NewRequest(method, d.URL, nil)
	if err != nil {
		return nil, err
	}
	if rangeHeader != "" {
		req.Header.Set("Range", rangeHeader)
	}
	if d.UserAgent != "" {
		req.Header.Set("User-Agent", d.UserAgent)
	}
	_ = rangeStart
	_ = rangeEnd
	return req, nil
}

func (d *Downloader) do(req *http.Request) (*http.Response, error) {
	client := d.HTTPClient
	if client == nil {
		client = defaultDownloadHTTPClient
	}
	return client.Do(req)
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func copyWithProgress(ctx context.Context, dst io.Writer, src io.Reader, onBytes func(int64)) error {
	buf := make([]byte, downloadBufferSizeBytes)
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		n, readErr := src.Read(buf)
		if n > 0 {
			if _, err := dst.Write(buf[:n]); err != nil {
				return err
			}
			if onBytes != nil {
				onBytes(int64(n))
			}
		}
		if readErr == io.EOF {
			return nil
		}
		if readErr != nil {
			return readErr
		}
	}
}

func replaceDownloadedFile(tmpPath string, destPath string) error {
	_ = os.Remove(destPath)
	return os.Rename(tmpPath, destPath)
}

type downloadProgressReporter struct {
	total      int64
	threads    int
	downloaded int64
	callback   func(DownloadProgressUpdate)

	mu          sync.Mutex
	lastEmit    time.Time
	lastPercent int
	lastBytes   int64
	lastSpeed   int64
}

func newDownloadProgressReporter(total int64, threads int, callback func(DownloadProgressUpdate)) *downloadProgressReporter {
	return &downloadProgressReporter{
		total:       total,
		threads:     threads,
		callback:    callback,
		lastPercent: -1,
	}
}

func (r *downloadProgressReporter) add(n int64) {
	if n <= 0 {
		return
	}
	atomic.AddInt64(&r.downloaded, n)
	r.emit(false)
}

func (r *downloadProgressReporter) finish() {
	if r.total > 0 {
		atomic.StoreInt64(&r.downloaded, r.total)
	}
	r.emit(true)
}

func (r *downloadProgressReporter) emit(done bool) {
	if r == nil || r.callback == nil {
		return
	}
	downloaded := atomic.LoadInt64(&r.downloaded)
	percent := calculateDownloadPercent(downloaded, r.total, done)

	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now()
	speed := r.lastSpeed
	if r.lastEmit.IsZero() {
		speed = 0
	} else {
		elapsed := now.Sub(r.lastEmit)
		if elapsed > 0 {
			speed = int64(float64(downloaded-r.lastBytes) / elapsed.Seconds())
			if speed < 0 {
				speed = 0
			}
		}
	}
	if !done && r.lastPercent == percent && now.Sub(r.lastEmit) < downloadProgressTick {
		return
	}
	if !done && r.lastEmit.IsZero() == false && now.Sub(r.lastEmit) < downloadProgressTick && percent < 100 {
		return
	}
	r.lastEmit = now
	r.lastPercent = percent
	r.lastBytes = downloaded
	r.lastSpeed = speed
	r.callback(DownloadProgressUpdate{
		Downloaded: downloaded,
		Total:      r.total,
		Speed:      speed,
		Percent:    percent,
		Threads:    r.threads,
		Done:       done,
	})
}

func calculateDownloadPercent(downloaded int64, total int64, done bool) int {
	if done {
		return 100
	}
	if total <= 0 {
		return 0
	}
	percent := int(float64(downloaded) * 100 / float64(total))
	if percent < 0 {
		return 0
	}
	if percent > 100 {
		return 100
	}
	return percent
}

func formatBytes(size int64) string {
	if size < 0 {
		size = 0
	}
	units := []string{"B", "KB", "MB", "GB", "TB"}
	value := float64(size)
	unit := 0
	for value >= 1024 && unit < len(units)-1 {
		value /= 1024
		unit++
	}
	if unit == 0 {
		return fmt.Sprintf("%d %s", size, units[unit])
	}
	return fmt.Sprintf("%.2f %s", value, units[unit])
}
