lappend ::auto_path [pwd]
package require tkxwin

set text {hello}

::tkxwin::registerHotkey Control-a {::tkxwin::sendUnicode $text}
pack [label .l -text {Press Control-a key to send text to active window}]
pack [entry .e -textvariable text]
