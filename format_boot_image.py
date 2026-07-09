import sys

with open('boot_images/demolition.txt', 'r') as f:
    lines = f.readlines()

out = []
out.append('#ifndef BOOT_IMAGE_H\n')
out.append('#define BOOT_IMAGE_H\n')
out.append('#include <stdint.h>\n')

for line in lines:
    if line.startswith('#include') or line.startswith('#if'):
        continue
    if 'extern const' in line:
        line = 'static const uint16_t boot_image[] = {\n'
    out.append(line)

# Add closing brace if missing, just in case
if not any('};' in l for l in out[-5:]):
    out.append('\n};\n')

out.append('\n#endif\n')

with open('main/boot_image.h', 'w') as f:
    f.writelines(out)
