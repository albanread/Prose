// The BMessage protocol between an editor and clangd_server. The server
// (src/main.cpp) implements it; the example client (src/client.cpp) and
// Sisong's language support speak it.
#ifndef PROSE_LSP_PROTOCOL_H
#define PROSE_LSP_PROTOCOL_H

#define CLANGD_SERVER_SIGNATURE	"application/x-vnd.prose.clangd_server"

#define LSP_SESSION			'LSes'	// -> 'LSsr': session (int32)
#define LSP_SESSION_REPLY	'LSsr'
#define LSP_OPEN			'LOpn'	// session, name, text
#define LSP_CHANGE			'LChg'	// session, name, text
#define LSP_CLOSE			'LCls'	// session, name
#define LSP_COMPLETE		'LCmp'	// session, name, line, col, reqid
									// -> 'LCmr' pushed to notify:
									//    reqid, count, label#i, kind#i,
									//    detail#i
#define LSP_COMPLETE_REPLY	'LCmr'
#define LSP_DEFINITION		'LDef'	// session, name, line, col, reqid
									// -> 'LDfr' pushed to notify:
									//    reqid, found, path, line, col
#define LSP_DEFINITION_REPLY 'LDfr'
#define LSP_END				'LEnd'	// session

// pushed to the notify messenger named when the session was opened:
// name, count, line#i, col#i, sev#i, text#i (byte positions)
#define LSP_DIAGNOSTICS		'LDgn'

#endif	// PROSE_LSP_PROTOCOL_H
