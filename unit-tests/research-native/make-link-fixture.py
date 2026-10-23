from pathlib import Path
import argparse
p=argparse.ArgumentParser(description='Generate an independent two-page PDF for link metadata and backend lock tests.')
p.add_argument('directory',type=Path,help='Private application fixtures/pdf directory')
root=p.parse_args().directory;root.mkdir(parents=True,exist_ok=True)
objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>',b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Annots [5 0 R 6 0 R] >>',b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] >>',b'<< /Type /Annot /Subtype /Link /Rect [20 700 200 725] /Contents (Open reference) /NM (reference) /H /P /A << /S /URI /URI (https://example.test/reference) >> >>',b'<< /Type /Annot /Subtype /Link /Rect [20 650 200 675] /Contents (Go to page two) /NM (page-two) /H /I /Dest [4 0 R /Fit] >>']
data=b'%PDF-1.5\n';offsets=[0]
for i,obj in enumerate(objects,1):offsets.append(len(data));data+=str(i).encode()+b' 0 obj\n'+obj+b'\nendobj\n'
xref=len(data);data+=b'xref\n0 7\n0000000000 65535 f \n'+b''.join(f'{x:010} 00000 n \n'.encode() for x in offsets[1:])+f'trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(root/'link-metadata.pdf').write_bytes(data)
print('Independent link PDF created')
