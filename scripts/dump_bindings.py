import glob, os, re

ROOT = r"C:\Users\Cwl\Desktop\stm32-zephyr\zephyr\dts\bindings"
want = [
    "memory-controllers/st,stm32-fmc-sdram.yaml",
    "memory-controllers/st,stm32-fmc.yaml",
    "mipi-dbi/st,stm32-fmc-mipi-dbi.yaml",
    "display/ilitek,ili9341.yaml",
    "input/xptek,xpt2046.yaml",
    "mtd/atmel,at24.yaml",
    "sensor/qst,qmi8658a.yaml",
    "video/ovti,ov5640.yaml",
    "video/st,stm32-dcmi.yaml",
    "sdhc/st,stm32-sdmmc.yaml",
    "can/st,stm32-bxcan.yaml",
]

for rel in want:
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        print("MISS", rel)
        continue
    text = open(path, encoding="utf-8").read()
    compat = re.search(r"compatible:\s*(.*)", text)
    req = re.search(r"required:\s*\n((?:\s*-\s*\S+\n)+)", text)
    props = re.findall(r"\n  ([a-z0-9\-]+):\s*\n?\s*type:", text)
    print("=" * 70)
    print(rel, "|", compat.group(1).strip() if compat else "?")
    print("  required:", ", ".join(x.strip("- ").strip() for x in req.group(1).splitlines()) if req else "(none)")
    print("  props:", ", ".join(props))

# 找不到 pcf8574 / eth / sai / usb 的绑定文件，按关键字搜
for kw in ["pcf8574", "st,stm32-ethernet", "st,stm32-i2s", "st,stm32-sai", "st,stm32-usb"]:
    hits = [p for p in glob.glob(os.path.join(ROOT, "**", "*.yaml"), recursive=True) if kw in os.path.basename(p)]
    print(kw, "->", [os.path.relpath(p, ROOT) for p in hits])
