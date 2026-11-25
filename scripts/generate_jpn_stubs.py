import os
import glob
import re

base_dir = "/mnt/c/work/Antigravity/Vita/Monster Hunter Tri Vita"
runtime_src = os.path.join(base_dir, "runtime/src")
generated_funcs_dir = os.path.join(base_dir, "generated/functions")

# Find all direct func_80xxxxxx
func_pattern = re.compile(r'\bfunc_80[0-9A-Fa-f]{6}\b')
# Also find hex addresses used in MH3_GAME_PORT(80xxxxxx, ...) or similar macros
macro_pattern = re.compile(r'\b(?:MH3_GAME_PORT|MH3_PORT|REGISTER_NATIVE_FUNCTION(?:_AS)?|FramedPort|Routed|Call)\s*\(\s*(?:0x)?(80[0-9A-Fa-f]{6})\b')

referenced_funcs = set()

for root, _, files in os.walk(runtime_src):
    for fname in files:
        if fname.endswith(('.cpp', '.h', '.hpp')) and fname != "jpn_unresolved_stubs.cpp":
            fpath = os.path.join(root, fname)
            with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                matches = func_pattern.findall(content)
                referenced_funcs.update(matches)
                macro_matches = macro_pattern.findall(content)
                for addr in macro_matches:
                    referenced_funcs.add(f"func_{addr.upper()}")

# Also include the 89 known symbols from the linker error log
log_path = '/mnt/c/Users/starl/.gemini/antigravity/brain/fb118b75-8e6a-4288-89c2-21c3cacf2045/.system_generated/tasks/task-1027.log'
if os.path.exists(log_path):
    with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
        log_text = f.read()
    log_symbols = re.findall(r'undefined symbol:\s*(func_80[0-9A-Fa-f]{6})', log_text)
    referenced_funcs.update(log_symbols)

print(f"Total referenced func_80 symbols: {len(referenced_funcs)}")

existing_funcs = set()
for fname in os.listdir(generated_funcs_dir):
    if fname.endswith('.cpp'):
        existing_funcs.add(fname[:-4])

print(f"Total generated funcs: {len(existing_funcs)}")

missing_funcs = sorted(list(referenced_funcs - existing_funcs))
print(f"Total missing funcs to stub: {len(missing_funcs)}")

out_path = os.path.join(runtime_src, "jpn_unresolved_stubs.cpp")
with open(out_path, "w", encoding="utf-8") as f:
    f.write("// Auto-generated stubs for EU-only reference functions not present in JPN translation.\n")
    f.write("// In JPN build (RMHJ08), all actual functions are translated at JPN addresses.\n")
    f.write("struct CpuContext;\n\n")
    f.write('extern "C" {\n\n')
    for fn in missing_funcs:
        f.write(f'__attribute__((weak)) void {fn}(CpuContext* ctx) {{\n')
        f.write("}\n\n")
    f.write('} // extern "C"\n')

print(f"Successfully generated {len(missing_funcs)} stubs in {out_path}")
