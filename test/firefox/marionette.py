import json, socket, sys, base64, time
class M:
    def __init__(s, port=2828):
        for i in range(60):
            try: s.s = socket.create_connection(('127.0.0.1', port)); break
            except OSError: time.sleep(1)
        s.recv(); s.id = 0
        s.cmd('WebDriver:NewSession', {'capabilities': {}})
    def recv(s):
        n = b''
        while not n.endswith(b':'): n += s.s.recv(1)
        ln = int(n[:-1]); d = b''
        while len(d) < ln: d += s.s.recv(ln - len(d))
        return json.loads(d)
    def cmd(s, name, params):
        s.id += 1; m = json.dumps([0, s.id, name, params]).encode()
        s.s.sendall(str(len(m)).encode() + b':' + m)
        r = s.recv()
        if r[2]: raise RuntimeError(r[2])
        return r[3]
    def chrome(s): s.cmd('Marionette:SetContext', {'value': 'chrome'})
    def js(s, script, *args): return s.cmd('WebDriver:ExecuteScript', {'script': script, 'args': list(args)})['value']
    def shot(s, path, full=True):
        b = s.cmd('WebDriver:TakeScreenshot', {'full': full, 'hash': False})['value']
        open(path, 'wb').write(base64.b64decode(b))
