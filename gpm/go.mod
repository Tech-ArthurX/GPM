module github.com/SECTL/GPM/gpm

go 1.25.1

require (
	github.com/SECTL/GPM/gs v0.0.0
	github.com/gorilla/websocket v1.5.3
	golang.org/x/text v0.38.0
)

require golang.org/x/sys v0.39.0 // indirect

replace github.com/SECTL/GPM/gs => ../gs
