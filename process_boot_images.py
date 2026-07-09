import os
import glob

# Find all txt files with size > 0
txt_files = [f for f in glob.glob('boot_images/*.txt') if os.path.getsize(f) > 0]

out = []
out.append('#ifndef BOOT_IMAGES_H\n')
out.append('#define BOOT_IMAGES_H\n')
out.append('#include <stdint.h>\n\n')

image_names = []

for txt_file in txt_files:
    basename = os.path.splitext(os.path.basename(txt_file))[0]
    var_name = f"img_{basename}"
    image_names.append(var_name)
    
    with open(txt_file, 'r') as f:
        lines = f.readlines()
        
    for line in lines:
        if line.startswith('#include') or line.startswith('#if') or line.startswith('#endif'):
            continue
        if 'extern const' in line:
            line = f'static const uint16_t {var_name}[] = {{\n'
        out.append(line)
    
    if not any('};' in l for l in out[-5:]):
        out.append('\n};\n\n')
    else:
        out.append('\n')

out.append('static const uint16_t* const boot_images[] = {\n')
for name in image_names:
    out.append(f'    {name},\n')
out.append('};\n\n')

out.append(f'#define NUM_BOOT_IMAGES {len(image_names)}\n')
out.append('\n#endif\n')

with open('main/boot_images.h', 'w') as f:
    f.writelines(out)
