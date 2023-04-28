lappend ::auto_path [pwd]
package require tkxwin

proc myproc {args} {
	puts $args
}

pack [button .b1 -text "after 3 seconds, grab active window" -command {
	after 3000 {::tkxwin::grabKey [::tkxwin::getActiveWindowId] myproc}
}]

pack [button .b2 -text "after 3 seconds, ungrab active window" -command {
	after 3000 {::tkxwin::ungrabKey [::tkxwin::getActiveWindowId]}
}]
