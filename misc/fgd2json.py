#!/usr/bin/env python3
"""
FGD -> JSON
用法:
    python fgd2json.py base.fgd halflife2.fgd -o out_dir/
输出:
    out_dir/<name>.json  每个输入对应一个 json（递归展开 include）
"""
import sys, os, re, json, argparse

# ---------- Tokenizer ----------
class Tok:
    def __init__(self, s, i): self.s, self.i = s, i
    def eof(self): return self.i >= len(self.s)
    def peek(self): return self.s[self.i] if not self.eof() else ''
    def next(self): c = self.peek(); self.i += 1; return c

def skip_ws(t):
    while not t.eof():
        if t.peek() in ' \t\r\n': t.i += 1
        elif t.peek() == '/' and t.i+1 < len(t.s) and t.s[t.i+1] == '/':
            while not t.eof() and t.peek() != '\n': t.i += 1
        elif t.peek() == '/' and t.i+1 < len(t.s) and t.s[t.i+1] == '*':
            t.i += 2
            while t.i+1 < len(t.s) and not (t.s[t.i] == '*' and t.s[t.i+1] == '/'): t.i += 1
            t.i += 2
        else: break

def read_ident(t):
    skip_ws(t)
    start = t.i
    while not t.eof() and (t.peek().isalnum() or t.peek() == '_'): t.i += 1
    return t.s[start:t.i]

def read_string(t):
    """读一个引号字符串（"..."），返回内容。没有引号时读一个 token。"""
    skip_ws(t)
    if t.peek() == '"':
        t.i += 1
        buf = []
        while not t.eof() and t.peek() != '"':
            if t.peek() == '\\' and t.i+1 < len(t.s):
                t.i += 1
                buf.append(t.next())
            else:
                buf.append(t.next())
        t.i += 1  # closing "
        return ''.join(buf)
    # 无引号：读到空白 / 特殊符号
    start = t.i
    specials = ' \t\r\n[]()":=,+'
    while not t.eof() and t.peek() not in specials: t.i += 1
    return t.s[start:t.i]

def read_string_concat(t):
    """读一个引号字符串，支持 "a" + "b" 拼接"""
    s = read_string(t)
    while True:
        save = t.i
        skip_ws(t)
        if t.peek() == '+':
            t.i += 1
            s += read_string(t)
        else:
            t.i = save
            break
    return s

def expect(t, ch):
    skip_ws(t)
    if t.peek() != ch:
        raise SyntaxError(f"expected '{ch}' at {t.i}, got '{t.peek()}'")
    t.i += 1

# ---------- Parser ----------
class FgdParser:
    def __init__(self, path, include_stack=None):
        self.path = path
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            self.text = f.read()
        self.t = Tok(self.text, 0)
        self.includes = []
        self.classes  = {}
        self.include_stack = include_stack or []

    def parse(self):
        while True:
            skip_ws(self.t)
            if self.t.eof(): break
            if self.t.peek() != '@':
                self.t.i += 1
                continue
            self.t.i += 1
            kw = read_ident(self.t)
            if kw == 'include':
                path = read_string(self.t)
                self._handle_include(path)
            elif kw in ('BaseClass', 'PointClass', 'SolidClass', 'NPCClass',
                        'FilterClass', 'KeyFrameClass', 'MoveClass'):
                self._parse_class(kw)
            elif kw in ('mapsize', 'materialexclusion', 'autovisgroup'):
                skip_ws(self.t)
                if self.t.peek() == '(':
                    depth = 0
                    while not self.t.eof():
                        c = self.t.next()
                        if c == '(': depth += 1
                        elif c == ')':
                            depth -= 1
                            if depth == 0: break
            else:
                while not self.t.eof() and self.t.peek() != '@': self.t.i += 1
        return self.classes, self.includes

    def _handle_include(self, rel):
        base_dir = os.path.dirname(os.path.abspath(self.path))
        p = os.path.normpath(os.path.join(base_dir, rel))
        if p in self.include_stack:
            print(f"[fgd2json] skip cyclic include: {p}")
            return
        if not os.path.exists(p):
            print(f"[fgd2json] WARN include not found: {p}")
            return
        self.includes.append(p)
        sub = FgdParser(p, self.include_stack + [p])
        sub_classes, sub_includes = sub.parse()
        for k, v in sub_classes.items():
            self.classes.setdefault(k, v)
        self.includes.extend(sub_includes)

    def _parse_class(self, kind):
        skip_ws(self.t)
        bases, size, studio, color = [], None, None, None
        while True:
            skip_ws(self.t)
            if self.t.eof() or self.t.peek() == '=': break
            ident = read_ident(self.t)
            if not ident:
                break
            skip_ws(self.t)
            if self.t.peek() == '(':
                arg = self._read_paren_group()
                if ident == 'base':
                    bases = [x.strip() for x in arg.split(',') if x.strip()]
                elif ident == 'size':
                    size = arg
                elif ident == 'studio':
                    studio = arg
                elif ident == 'color':
                    color = arg
                # 其他 helper 忽略

        expect(self.t, '=')
        classname = read_ident(self.t)
        skip_ws(self.t)
        desc = ''
        if self.t.peek() == ':':
            self.t.i += 1
            desc = read_string_concat(self.t)
        skip_ws(self.t)
        if self.t.peek() != '[':
            self._record(kind, classname, bases, size, studio, color, desc, [])
            return
        self.t.i += 1  # '['
        kvs = self._parse_body()
        self._record(kind, classname, bases, size, studio, color, desc, kvs)

    def _read_paren_group(self):
        assert self.t.peek() == '('
        self.t.i += 1
        depth = 1
        start = self.t.i
        while not self.t.eof() and depth > 0:
            c = self.t.next()
            if c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    return self.t.s[start:self.t.i-1]
        return self.t.s[start:]

    def _unquote(self, s):
        s = s.strip()
        if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
            return s[1:-1]
        return s

    def _parse_body(self):
        """解析 [ ... ] 里的 keyvalue 列表。返回 [ {name,type,desc,default,long,choices[]} ]"""
        items = []
        while True:
            skip_ws(self.t)
            if self.t.eof(): break
            c = self.t.peek()
            if c == ']':
                self.t.i += 1
                break
            # input/output 行直接跳过
            save = self.t.i
            ident = read_ident(self.t)
            if ident in ('input', 'output'):
                skip_ws(self.t)
                read_ident(self.t)  # name
                skip_ws(self.t)
                if self.t.peek() == '(':
                    self._read_paren_group()
                skip_ws(self.t)
                if self.t.peek() == ':':
                    self.t.i += 1
                    read_string_concat(self.t)
                continue
            if not ident:
                self.t.i += 1
                continue
            # keyvalue
            name = ident
            skip_ws(self.t)
            if self.t.peek() != '(':
                continue
            ktype = self._read_paren_group().strip()
            ktype = ktype.split()[0] if ktype else ''
            kv = {'name': name, 'type': ktype, 'desc': '', 'default': '', 'long': '', 'choices': []}
            skip_ws(self.t)
            if self.t.peek() == ':':
                self.t.i += 1
                kv['desc'] = read_string_concat(self.t)
            skip_ws(self.t)
            if self.t.peek() == ':':
                self.t.i += 1
                kv['default'] = read_string_concat(self.t)
            skip_ws(self.t)
            if self.t.peek() == ':':
                self.t.i += 1
                kv['long'] = read_string_concat(self.t)
            skip_ws(self.t)
            if self.t.peek() == '=':
                self.t.i += 1
                skip_ws(self.t)
                if self.t.peek() == '[':
                    self.t.i += 1
                    while True:
                        skip_ws(self.t)
                        if self.t.eof(): break
                        if self.t.peek() == ']':
                            self.t.i += 1
                            break
                        # value : "caption" : default
                        val = read_string(self.t)          # ← 修正：self.t
                        skip_ws(self.t)
                        caption = ''
                        if self.t.peek() == ':':
                            self.t.i += 1
                            caption = read_string_concat(self.t)
                        skip_ws(self.t)
                        if self.t.peek() == ':':
                            self.t.i += 1
                            read_string_concat(self.t)      # 默认值忽略
                        kv['choices'].append({'value': val, 'caption': caption or val})
            items.append(kv)
        return items

    def _record(self, kind, classname, bases, size, studio, color, desc, kvs):
        self.classes[classname] = {
            'kind': kind,
            'bases': bases,
            'size': self._unquote(size) if size else '',
            'studio': self._unquote(studio) if studio else '',
            'color': self._unquote(color) if color else '',
            'description': desc,
            'keyvalues': kvs,
        }


def resolve_inheritance(classes):
    """递归合并 base class 的 keyvalues。子类同名覆盖。"""
    resolved = {}
    def resolve(name, stack):
        if name in resolved:
            return resolved[name]
        if name in stack:
            print(f"[fgd2json] WARN cyclic base: {name}")
            return classes.get(name, {})
        c = classes.get(name)
        if not c:
            return {}
        stack = stack + [name]
        merged_kv = {}
        for b in c.get('bases', []):
            base_cls = resolve(b, stack)
            for kv in base_cls.get('keyvalues', []):
                merged_kv[kv['name']] = kv
        for kv in c.get('keyvalues', []):
            merged_kv[kv['name']] = kv
        out = dict(c)
        out['keyvalues'] = list(merged_kv.values())
        resolved[name] = out
        return out

    for name in list(classes.keys()):
        resolve(name, [])
    return resolved


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('-o', '--out', default='.')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    all_classes = {}
    for inp in args.inputs:
        p = FgdParser(inp)
        cs, _ = p.parse()
        for k, v in cs.items():
            all_classes.setdefault(k, v)
        print(f"[fgd2json] parsed {inp}: {len(cs)} classes")

    resolved = resolve_inheritance(all_classes)

    out_name = os.path.splitext(os.path.basename(args.inputs[0]))[0] + '.json'
    out_path = os.path.join(args.out, out_name)
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(resolved, f, indent=2, ensure_ascii=False)
    print(f"[fgd2json] wrote {out_path} ({len(resolved)} classes)")


if __name__ == '__main__':
    main()