package ir

import (
	"encoding/xml"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type uiWindow struct {
	XMLName  xml.Name    `xml:"window"`
	Title    string      `xml:"title,attr"`
	Width    int         `xml:"width,attr"`
	Height   int         `xml:"height,attr"`
	Class    string      `xml:"class,attr"`
	Style    string      `xml:"style,attr"`
	Controls []uiControl `xml:",any"`
}

type uiControl struct {
	XMLName xml.Name
	ID      string `xml:"id,attr"`
	Text    string `xml:"text,attr"`
	X       int    `xml:"x,attr"`
	Y       int    `xml:"y,attr"`
	W       int    `xml:"w,attr"`
	H       int    `xml:"h,attr"`
	Style   string `xml:"style,attr"`
}

func (g *Generator) genUIDef(args []string) error {
	name, rel := splitKV(args)
	if name == "" || rel == "" {
		return nil
	}
	path := rel
	if !filepath.IsAbs(path) && g.srcDir != "" {
		path = filepath.Join(g.srcDir, rel)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("UIDF: read %s: %w", path, err)
	}
	var win uiWindow
	if err := xml.Unmarshal(data, &win); err != nil {
		return fmt.Errorf("UIDF: parse %s: %w", path, err)
	}
	g.emitWindow(name, &win)
	return nil
}

func (g *Generator) emitWindow(name string, w *uiWindow) {
	if w.Title == "" {
		w.Title = name
	}
	if w.Width == 0 {
		w.Width = 640
	}
	if w.Height == 0 {
		w.Height = 480
	}
	g.declare(name, TypeHandle)
	fmt.Fprintf(&g.code, "    if (!gs_lcl_load()) { gs_show_error(\"cannot load gs_lcl_runtime.dll\", \"gs lcl\"); return 2; }\n")
	fmt.Fprintf(&g.code, "    gs_lcl_init();\n")
	fmt.Fprintf(&g.code, "    %s = gs_lcl_form_new(%s, %d, %d);\n", name, cString(w.Title), w.Width, w.Height)
	for _, c := range w.Controls {
		g.emitControl(name, &c)
	}
	fmt.Fprintf(&g.code, "    gs_lcl_show(%s);\n", name)
}

func (g *Generator) emitControl(parent string, c *uiControl) {
	kind := strings.ToLower(c.XMLName.Local)
	if kind == "" {
		kind = "label"
	}
	if c.W == 0 {
		c.W = 80
	}
	if c.H == 0 {
		c.H = 24
	}
	fmt.Fprintf(&g.code, "    gs_lcl_control(%s, %s, %s, %d, %d, %d, %d);\n",
		parent, cString(kind), cString(c.Text), c.X, c.Y, c.W, c.H)
}

func (g *Generator) genUILoop(args []string) {
	fmt.Fprintf(&g.code, "    if (!gs_lcl_load()) { gs_show_error(\"cannot load gs_lcl_runtime.dll\", \"gs lcl\"); return 2; }\n")
	fmt.Fprintf(&g.code, "    gs_lcl_run();\n")
}
