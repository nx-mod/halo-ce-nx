/*
HOST_MAIN.C

Loads port/switch/guest/guest.elf (the first milestone - PORTING.md),
places it at its linked address with real memory (the mechanism
nx-mapmem-poc validated end to end: svcCreateCodeMemory +
svcControlCodeMemory's MapOwner/MapSlave dance - see that repo's commit
history for the two gotchas: cache maintenance, and writing through the
owner view, not the pre-create source buffer), resolves its one import,
and calls its entry point.

Everything here is a single R-X segment on purpose, including the
import table: the host patches it in before transitioning to the
executable (MapSlave) view, and the guest only ever reads it, never
writes. A real game needs a separate writable data segment too (a plain
svcMapMemory region, no CodeMemory dance) - follows once there's mutable
guest state to place.
*/

#include <elf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "../../include/switch_guest_abi.h"

static FILE *g_log;

static void logf_both(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	if (g_log)
	{
		va_start(args, fmt);
		vfprintf(g_log, fmt, args);
		va_end(args);
		fflush(g_log);
		fsdevCommitDevice("sdmc");
	}
}

/* ---------- the host side of the guest's imports */

static void host_log_impl(const char *text)
{
	logf_both("[guest] %s\n", text);
}

struct host_function
{
	const char *name;
	void *address;
};

static const struct host_function kHostFunctions[] = {
	{"host_log", (void *)host_log_impl},
};

static void *resolve_import(const char *name)
{
	for (size_t i = 0; i < sizeof(kHostFunctions) / sizeof(kHostFunctions[0]); i++)
		if (!strcmp(kHostFunctions[i].name, name))
			return kHostFunctions[i].address;
	return NULL;
}

/* ---------- loading */

static int load_and_run_guest(const char *path)
{
	FILE *file;
	long file_size;
	unsigned char *file_data;
	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdrs;
	Elf32_Phdr *load_segment = NULL;
	size_t mapped_size;
	void *owner_src;
	void *owner_view;
	Handle code_handle;
	Result rc;
	const struct guest_header *header_in_file;
	int missing_imports = 0;

	file = fopen(path, "rb");
	if (!file)
	{
		logf_both("can't open %s\n", path);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	file_data = malloc(file_size);
	if (!file_data || fread(file_data, 1, file_size, file) != (size_t)file_size)
	{
		logf_both("can't read %s\n", path);
		fclose(file);
		return -1;
	}
	fclose(file);
	logf_both("read guest.elf, %ld bytes\n", file_size);

	ehdr = (Elf32_Ehdr *)file_data;
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) || ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
		ehdr->e_machine != EM_AARCH64 || ehdr->e_type != ET_EXEC)
	{
		logf_both("guest.elf is not an ILP32 AArch64 executable\n");
		free(file_data);
		return -1;
	}

	phdrs = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
	for (int i = 0; i < ehdr->e_phnum; i++)
	{
		if (phdrs[i].p_type == PT_LOAD)
		{
			if (load_segment)
			{
				logf_both("guest.elf has more than one PT_LOAD segment - this loader only "
					"handles the single merged RWX segment of the first milestone\n");
				free(file_data);
				return -1;
			}
			load_segment = &phdrs[i];
		}
	}
	if (!load_segment)
	{
		logf_both("guest.elf has no PT_LOAD segment\n");
		free(file_data);
		return -1;
	}
	if (load_segment->p_vaddr != GUEST_IMAGE_BASE)
	{
		logf_both("guest.elf's segment is at 0x%x, expected 0x%x\n",
			load_segment->p_vaddr, GUEST_IMAGE_BASE);
		free(file_data);
		return -1;
	}

	mapped_size = (load_segment->p_memsz + 0xfff) & ~0xfffu;
	logf_both("PT_LOAD: vaddr=0x%x filesz=0x%x memsz=0x%x -> mapping 0x%zx bytes\n",
		load_segment->p_vaddr, load_segment->p_filesz, load_segment->p_memsz, mapped_size);

	owner_src = aligned_alloc(0x1000, mapped_size);
	if (!owner_src)
	{
		logf_both("aligned_alloc(0x%zx) failed\n", mapped_size);
		free(file_data);
		return -1;
	}

	rc = svcCreateCodeMemory(&code_handle, owner_src, mapped_size);
	if (R_FAILED(rc))
	{
		logf_both("svcCreateCodeMemory FAILED, rc=0x%x\n", rc);
		free(owner_src);
		free(file_data);
		return -1;
	}

	virtmemLock();
	owner_view = virtmemFindCodeMemory(mapped_size, 0x1000);
	virtmemUnlock();
	if (!owner_view)
	{
		logf_both("virtmemFindCodeMemory failed\n");
		svcCloseHandle(code_handle);
		free(owner_src);
		free(file_data);
		return -1;
	}

	rc = svcControlCodeMemory(code_handle, CodeMapOperation_MapOwner, owner_view, mapped_size, Perm_Rw);
	if (R_FAILED(rc))
	{
		logf_both("MapOwner FAILED, rc=0x%x\n", rc);
		svcCloseHandle(code_handle);
		free(owner_src);
		free(file_data);
		return -1;
	}

	/* the lesson from nx-mapmem-poc: write through the owner view, not
	owner_src - the pre-create buffer's content never reaches the real
	backing. */
	memset(owner_view, 0, mapped_size);
	memcpy(owner_view, file_data + load_segment->p_offset, load_segment->p_filesz);

	/* the header is link-time-constant content, already correct in the
	file at its linked offsets - read it straight from the file buffer
	rather than re-deriving addresses. */
	header_in_file = (const struct guest_header *)(file_data + load_segment->p_offset);
	if (header_in_file->magic != GUEST_MAGIC || header_in_file->abi_version != GUEST_ABI_VERSION)
	{
		logf_both("guest.elf's header doesn't match this host (magic/version)\n");
		svcControlCodeMemory(code_handle, CodeMapOperation_UnmapOwner, owner_view, mapped_size, 0);
		svcCloseHandle(code_handle);
		free(owner_src);
		free(file_data);
		return -1;
	}
	logf_both("guest header OK: image_end=0x%x import_table=0x%x import_names=0x%x "
		"import_count=%u entry=0x%x\n",
		header_in_file->image_end, header_in_file->import_table, header_in_file->import_names,
		header_in_file->import_count, header_in_file->entry);

	/* resolve imports, writing resolved addresses through owner_view at
	the same vaddr-relative offset the guest will read them from */
	{
		uint64_t *table = (uint64_t *)((char *)owner_view + (header_in_file->import_table - GUEST_IMAGE_BASE));
		const char *name = (const char *)owner_view + (header_in_file->import_names - GUEST_IMAGE_BASE);

		for (uint32_t i = 0; i < header_in_file->import_count; i++)
		{
			void *function = resolve_import(name);

			if (!function)
			{
				logf_both("guest import '%s' has no host implementation\n", name);
				missing_imports++;
			}
			else
			{
				logf_both("resolved import '%s' -> %p\n", name, function);
				table[i] = (uint64_t)(uintptr_t)function;
			}
			name += strlen(name) + 1;
		}
	}
	if (missing_imports)
	{
		svcControlCodeMemory(code_handle, CodeMapOperation_UnmapOwner, owner_view, mapped_size, 0);
		svcCloseHandle(code_handle);
		free(owner_src);
		free(file_data);
		return -1;
	}

	armDCacheFlush(owner_view, mapped_size);

	rc = svcControlCodeMemory(code_handle, CodeMapOperation_MapSlave, (void *)(uintptr_t)GUEST_IMAGE_BASE,
		mapped_size, Perm_Rx);
	if (R_FAILED(rc))
	{
		logf_both("MapSlave FAILED, rc=0x%x\n", rc);
		svcControlCodeMemory(code_handle, CodeMapOperation_UnmapOwner, owner_view, mapped_size, 0);
		svcCloseHandle(code_handle);
		free(owner_src);
		free(file_data);
		return -1;
	}
	armICacheInvalidate((void *)(uintptr_t)GUEST_IMAGE_BASE, mapped_size);

	logf_both("mapped at 0x%x, R-X. about to call entry 0x%x ...\n",
		GUEST_IMAGE_BASE, header_in_file->entry);
	{
		void (*entry)(void) = (void (*)(void))(uintptr_t)header_in_file->entry;
		entry();
	}
	logf_both("entry returned.\n");

	svcControlCodeMemory(code_handle, CodeMapOperation_UnmapSlave, (void *)(uintptr_t)GUEST_IMAGE_BASE,
		mapped_size, 0);
	svcControlCodeMemory(code_handle, CodeMapOperation_UnmapOwner, owner_view, mapped_size, 0);
	svcCloseHandle(code_handle);
	free(owner_src);
	free(file_data);
	return 0;
}

int main(int argc, char *argv[])
{
	consoleInit(NULL);

	g_log = fopen("sdmc:/switch/halo-ce-nx-guest-poc/host.log", "w");
	logf_both("halo-ce-nx guest-poc host starting\n");

	if (load_and_run_guest("sdmc:/switch/halo-ce-nx-guest-poc/guest.elf") == 0)
		logf_both("SUCCESS: the guest loaded, ran and called back into the host.\n");
	else
		logf_both("FAILED: see the lines above.\n");

	if (g_log)
		fclose(g_log);

	printf("Done. See sdmc:/switch/halo-ce-nx-guest-poc/host.log\nPress + to exit.\n");
	consoleUpdate(NULL);

	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	while (appletMainLoop())
	{
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
			break;
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
	return 0;
}
