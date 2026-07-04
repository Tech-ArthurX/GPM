@echo off
set CGO_ENABLED=0
set GOOS=windows
set GOARCH=amd64
go build -ldflags="-s -w" -o server.exe
echo Built server.exe for Windows
