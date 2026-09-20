#!/usr/bin/env python3
"""vmctl.py — keys, mouse and screenshots for a guitest.sh machine, over QMP.

Run through `guitest.sh ctl ...`, or from the machine's work directory (the
socket path is relative: a UNIX socket path is limited to 104 bytes).

  key alt-s [ret ...]      key combinations, QMP qcode names joined by '-'
                           (alt is Haiku's Command key: alt-s saves, alt-q quits;
                           ctrl, shift, ret, esc, tab, spc, up, down, left,
                           right, home, end, pgup, pgdn, delete, backspace,
                           f1..f12, compose = the Menu key)
  type 'text'              a string, US layout
  click X Y | dclick X Y | rclick X Y | move X Y       target pixels
  wheel up|down [n]        the mouse wheel, at the pointer
  shot name                screenshot -> name.png in the work directory
  sleep seconds
  hmp command...           any QEMU monitor command
Commands chain with '--':  click 300 300 -- type 'abc' -- key alt-s -- shot s1

Keys are queued by QEMU and the target takes a moment to act on them: put a
sleep before a shot that is to show their effect.
SCREEN_W/SCREEN_H (default 1024x768) scale pointer positions; KEY_DELAY
(default 0.06 s) is the pause after each key.
"""
import json, socket, sys, time, os

W, H = int(os.environ.get('SCREEN_W', 1024)), int(os.environ.get('SCREEN_H', 768))
DELAY = float(os.environ.get('KEY_DELAY', 0.06))

SHIFTED = {'(': '9', ')': '0', '{': 'bracket_left', '}': 'bracket_right', '<': 'comma', '>': 'dot',
	'"': 'apostrophe', ':': 'semicolon', '_': 'minus', '+': 'equal', '!': '1', '@': '2', '#': '3',
	'$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '?': 'slash', '|': 'backslash', '~': 'grave_accent'}
PLAIN = {' ': 'spc', '.': 'dot', ',': 'comma', '/': 'slash', ';': 'semicolon', '-': 'minus', '=': 'equal',
	"'": 'apostrophe', '[': 'bracket_left', ']': 'bracket_right', '\\': 'backslash', '`': 'grave_accent',
	'\n': 'ret', '\t': 'tab'}

class QMP:
	def __init__(self, path='qmp.sock'):
		self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.s.connect(path)
		self.f = self.s.makefile('rwb')
		self.read()
		self.cmd('qmp_capabilities')
	def read(self):
		while True:
			line = self.f.readline()
			if not line:
				raise ConnectionError('QMP closed')
			msg = json.loads(line)
			if 'event' not in msg:
				return msg
	def cmd(self, name, **args):
		obj = {'execute': name}
		if args:
			obj['arguments'] = args
		self.f.write(json.dumps(obj).encode() + b'\n'); self.f.flush()
		r = self.read()
		if 'error' in r:
			raise RuntimeError('%s: %s' % (name, r['error']))
		return r.get('return')
	def keys(self, combo):
		names = combo.split('-') if combo not in ('-',) else ['minus']
		keys = [{'type': 'qcode', 'data': n} for n in names]
		self.cmd('send-key', keys=keys, **{'hold-time': 40})
		time.sleep(DELAY)
	def type(self, text):
		for c in text:
			if c in SHIFTED: self.keys('shift-' + SHIFTED[c])
			elif c in PLAIN: self.keys(PLAIN[c])
			elif c.isupper(): self.keys('shift-' + c.lower())
			elif c.isalnum(): self.keys(c)
			else: print('cannot type %r' % c, file=sys.stderr)
	def move(self, x, y):
		ev = [{'type': 'abs', 'data': {'axis': 'x', 'value': int(x * 32767 / W)}},
			{'type': 'abs', 'data': {'axis': 'y', 'value': int(y * 32767 / H)}}]
		self.cmd('input-send-event', events=ev); time.sleep(0.05)
	def button(self, which, down):
		self.cmd('input-send-event', events=[{'type': 'btn', 'data': {'down': down, 'button': which}}])
		time.sleep(0.04)
	def click(self, x, y, which='left', times=1):
		self.move(x, y)
		for _ in range(times):
			self.button(which, True); self.button(which, False)
		time.sleep(0.15)

def main():
	args = sys.argv[1:]
	groups, cur = [], []
	for a in args:
		if a == '--':
			groups.append(cur); cur = []
		else:
			cur.append(a)
	groups.append(cur)
	q = QMP()
	for g in groups:
		if not g: continue
		c, rest = g[0], g[1:]
		if c == 'key':
			for k in rest: q.keys(k)
		elif c == 'type': q.type(rest[0])
		elif c == 'move': q.move(int(rest[0]), int(rest[1]))
		elif c == 'click': q.click(int(rest[0]), int(rest[1]))
		elif c == 'dclick': q.click(int(rest[0]), int(rest[1]), times=2)
		elif c == 'rclick': q.click(int(rest[0]), int(rest[1]), which='right')
		elif c == 'shot':
			path = os.path.abspath(rest[0] + '.png')
			q.cmd('screendump', filename=path, format='png'); time.sleep(0.3); print(path)
		elif c == 'wheel':
			# wheel up|down [n] at the current pointer position
			for _ in range(int(rest[1]) if len(rest) > 1 else 1):
				q.button('wheel-' + rest[0], True); q.button('wheel-' + rest[0], False)
		elif c == 'sleep': time.sleep(float(rest[0]))
		elif c == 'hmp': print(q.cmd('human-monitor-command', **{'command-line': ' '.join(rest)}))
		else: sys.exit('unknown command %s' % c)

main()
