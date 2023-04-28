lappend ::auto_path [pwd]
package require tkxwin

pack [button .b1 -text "make Control-a to exit" -command {
	::tkxwin::registerHotkey Control-a {exit}
}]

pack [button .b2 -text "make Control-a to do not exit" -command {
	::tkxwin::unregisterHotkey Control-a
}]
