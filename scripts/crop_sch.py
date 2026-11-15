import os, pymupdf

src = r"C:\Users\Cwl\Desktop\stm32-zephyr\docs\hardware\sch\DNF429P_F767P_H743P V2.6.pdf"
out = r"C:\Users\Cwl\Desktop\stm32-zephyr\logs\sch"
d = pymupdf.open(src)

jobs = [(3, "IO_EXPANDER"), (3, "6 AXIS SENSOR"), (3, "RS485"),
        (4, "BEEP"), (4, "USB_UART"), (3, "CAN")]

for pg, key in jobs:
    page = d[pg - 1]
    hits = page.search_for(key)
    if not hits:
        print("miss", key)
        continue
    b = hits[0]
    clip = pymupdf.Rect(max(0, b.x0 - 260), max(0, b.y0 - 130),
                        min(page.rect.width, b.x0 + 320), min(page.rect.height, b.y0 + 220))
    pix = page.get_pixmap(matrix=pymupdf.Matrix(6, 6), clip=clip, alpha=False)
    name = "p%d_%s.png" % (pg, key.replace(" ", "_"))
    path = os.path.join(out, name)
    pix.save(path)
    print(path, pix.width, pix.height)
