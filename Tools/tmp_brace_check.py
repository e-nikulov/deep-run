import re
import sys

path = r"c:\Users\es.nikulov\source\repos\deep-run\Tests\TestMain.cpp"
text = open(path, encoding="utf-8").read()

out = []
i = 0
n = len(text)
while i < n:
    c = text[i]
    if text.startswith("//", i):
        j = text.find("\n", i)
        if j < 0:
            break
        i = j
        continue
    if text.startswith("/*", i):
        j = text.find("*/", i + 2)
        seg = text[i : j + 2] if j >= 0 else text[i:]
        i = (j + 2) if j >= 0 else n
        out.append("\n" * seg.count("\n"))
        continue
    m = re.match(r'R"([^(]*)\(', text[i:])
    if m:
        delim = 'R"' + m.group(1) + "("
        end = text.find(')"', i + len(delim))
        seg = text[i : end + 2]
        out.append("\n" * seg.count("\n"))
        i = end + 2
        continue
    if c == '"' or c == "'":
        j = i + 1
        while j < n and text[j] != c:
            if text[j] == "\\":
                j += 1
            elif text[j] == "\n":
                break
            j += 1
        out.append("\n" * text[i : j].count("\n"))
        i = j + 1
        continue
    out.append(c)
    i += 1

clean = "".join(out)
depth = 0
for idx, line in enumerate(clean.split("\n"), 1):
    depth += line.count("{") - line.count("}")
print("final:", depth)
d = 0
for idx, line in enumerate(clean.split("\n"), 1):
    d += line.count("{") - line.count("}")
    if idx > 50 and d == 0:
        print("depth 0 at", idx)
