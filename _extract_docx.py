import zipfile, xml.etree.ElementTree as ET

z = zipfile.ZipFile(r'C:\Users\35015\Desktop\2026嵌入式大赛作品报告_智能手表.docx')
doc = z.read('word/document.xml')
root = ET.fromstring(doc)
ns = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
texts = []
for p in root.iter('{%s}p' % ns):
    line = ''.join(t.text for t in p.iter('{%s}t' % ns) if t.text)
    if line.strip():
        texts.append(line)

with open(r'C:\Users\35015\Desktop\report_text.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(texts))
print(f'Extracted {len(texts)} paragraphs')
