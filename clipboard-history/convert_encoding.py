import re, glob, os

def convert_cjk(c):
    o = ord(c)
    if 0x4E00 <= o <= 0x9FFF or 0x3400 <= o <= 0x4DBF or 0xF900 <= o <= 0xFAFF:
        return '\\u%04X' % o
    return c

def process_literal(m):
    return m.group(1) + ''.join(convert_cjk(c) for c in m.group(2)) + m.group(3)

for fpath in glob.glob('d:/aaa/clipboard-history/src/*.cpp'):
    with open(fpath, 'r', encoding='utf-8-sig') as f:
        content = f.read()
    new_content = re.sub(r'(L")([^"]*)(")', process_literal, content)
    if new_content != content:
        with open(fpath, 'w', encoding='utf-8-sig') as f:
            f.write(new_content)
        print('Converted:', fpath)
    else:
        print('No change:', fpath)
