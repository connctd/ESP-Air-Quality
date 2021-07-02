# ESP-Air-Quality

# Dependencies

## Libraries

### Wifi Manager

### BSEC 


#### Error Handling

change ```platform.txt```, search for line

		recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"

and change it to 

		recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {compiler.libraries.ldflags} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"