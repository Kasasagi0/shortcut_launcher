#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include "ico2png.h"
#include "launcher_html.h"

/*
  Folder layout (next to the exe):

    ShortcutMaker.exe
    shortcut_html\   <- the generated .html files go here
    shortcut_icon\   <- the copied .ico files AND the .png thumbnails go here

  The html uses the relative path ../shortcut_icon/<id>.ico,
  so you can move the whole folder anywhere and it keeps working.
*/

// How long (in milliseconds) the page waits before closing itself.
// 0 = close instantly. If a game ever fails to launch, try 100 or 200.
#define CLOSE_DELAY_MS 0

// The first time a page is opened in the browser it loads its icon and waits
// this long before launching, so the browser has time to save the icon.
// After that it launches straight away.
#define SETUP_DELAY_MS 3000

static char exeDir[MAX_PATH];   // folder the exe is in

// Find the folder the exe lives in (not the "current" folder)
static void get_exe_dir(void) {
	GetModuleFileNameA(NULL, exeDir, MAX_PATH);
	char* slash = strrchr(exeDir, '\\');
	if (slash) *slash = '\0';
}

static int dir_exists(const char* p) {
	DWORD a = GetFileAttributesA(p);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Converts ONE steam .url shortcut into an html file.
// Returns 1 on success, 0 if skipped or failed.
static int convert_shortcut(const char* urlPath) {
	FILE* f = fopen(urlPath, "r");
	if (!f) {
		printf("  [fail] Could not open %s\n", urlPath);
		return 0;
	}

	// shortcut name = file name without the folder and without ".url"
	char name[MAX_PATH];
	const char* base = strrchr(urlPath, '\\');
	snprintf(name, sizeof name, "%s", base ? base + 1 : urlPath);
	char* dot = strrchr(name, '.');
	if (dot) *dot = '\0';

	// 1. read the id and the icon path from the shortcut
	char line[1024];
	char id[64] = "";
	char icon[MAX_PATH] = "";

	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';

		if (strncmp(line, "URL=steam://rungameid/", 22) == 0) {
			snprintf(id, sizeof id, "%s", line + 22);
		}
		else if (strncmp(line, "IconFile=", 9) == 0) {
			snprintf(icon, sizeof icon, "%s", line + 9);
		}
	}
	fclose(f);

	if (id[0] == '\0') {
		printf("  [skip] %s (not a steam game shortcut)\n", name);
		return 0;
	}

	// 2. copy the icon into shortcut_icon\<id>.ico, then make a small
	//    shortcut_icon\<id>.png from it. The html page finds that png on the
	//    website by itself, so upload the shortcut_icon folder too.
	//    (the id is always plain digits, so the file names are always safe)
	int haveIcon = 0;
	if (icon[0] != '\0') {
		char iconDest[MAX_PATH * 2];
		snprintf(iconDest, sizeof iconDest, "%s\\shortcut_icon\\%s.ico", exeDir, id);

		if (CopyFileA(icon, iconDest, FALSE)) {   // FALSE = overwrite if it exists
			char pngDest[MAX_PATH * 2];
			snprintf(pngDest, sizeof pngDest, "%s\\shortcut_icon\\%s.png", exeDir, id);

			if (ico_to_favicon_png(iconDest, pngDest, 64)) {
				haveIcon = 1;
			}
			else {
				printf("  [warn] %s: could not make the png icon\n", name);
			}
		}
		else {
			printf("  [warn] %s: could not copy icon (%s)\n", name, icon);
		}
	}

	// 3. write shortcut_html\<name>.html
	char htmlPath[MAX_PATH * 2];
	snprintf(htmlPath, sizeof htmlPath, "%s\\shortcut_html\\%s.html", exeDir, name);

	FILE* out = fopen(htmlPath, "w");
	if (!out) {
		printf("  [fail] %s: could not create %s\n", name, htmlPath);
		return 0;
	}

	write_launcher_html(out, haveIcon, id, CLOSE_DELAY_MS, SETUP_DELAY_MS);

	fclose(out);
	printf("  [ ok ] %s  (id %s)\n", name, id);
	return 1;
}

// Opens the normal "pick a file" window
static int pick_file(char* path) {
	OPENFILENAMEA ofn = { 0 };
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = "Internet Shortcuts (*.url)\0*.url\0All Files (*.*)\0*.*\0";
	ofn.lpstrTitle = "Choose a Steam shortcut";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	return GetOpenFileNameA(&ofn);
}

// Opens the "pick a folder" window
static int pick_folder(char* path) {
	BROWSEINFOA bi = { 0 };
	bi.lpszTitle = "Choose the folder with your Steam shortcuts";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	CoInitialize(NULL);
	LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
	if (!pidl) return 0;

	SHGetPathFromIDListA(pidl, path);
	CoTaskMemFree(pidl);
	return 1;
}

// Converts every .url file inside a folder (not its sub folders)
static void convert_folder(const char* folder) {
	char pattern[MAX_PATH + 8];
	snprintf(pattern, sizeof pattern, "%s\\*.url", folder);

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		printf("No .url files found in that folder.\n");
		return;
	}

	int total = 0, done = 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

		// "*.url" can also match things like "x.urlbackup", so check the end
		const char* ext = strrchr(fd.cFileName, '.');
		if (!ext || _stricmp(ext, ".url") != 0) continue;

		char full[MAX_PATH * 2];
		snprintf(full, sizeof full, "%s\\%s", folder, fd.cFileName);

		total++;
		done += convert_shortcut(full);
	} while (FindNextFileA(h, &fd));
	FindClose(h);

	printf("\nConverted %d of %d shortcuts.\n", done, total);
}

int main(void) {
	get_exe_dir();

	// the two folders must exist next to the exe
	char check[MAX_PATH * 2];
	snprintf(check, sizeof check, "%s\\shortcut_html", exeDir);
	int okHtml = dir_exists(check);
	snprintf(check, sizeof check, "%s\\shortcut_icon", exeDir);
	int okIcon = dir_exists(check);

	if (!okHtml || !okIcon) {
		printf("Missing folder(s) next to the exe:\n");
		if (!okHtml) printf("  shortcut_html\n");
		if (!okIcon) printf("  shortcut_icon\n");
		printf("\nCreate them in: %s\n", exeDir);
		printf("\nPress Enter to exit...");
		getchar();
		return 1;
	}

	printf("Steam shortcut -> html\n");
	printf("  1 = convert one shortcut\n");
	printf("  2 = convert every shortcut in a folder\n");
	printf("Choice: ");

	char answer[16];
	if (!fgets(answer, sizeof answer, stdin)) return 1;

	char path[MAX_PATH] = "";

	if (answer[0] == '1') {
		if (pick_file(path)) convert_shortcut(path);
		else printf("Nothing picked.\n");
	}
	else if (answer[0] == '2') {
		if (pick_folder(path)) convert_folder(path);
		else printf("Nothing picked.\n");
	}
	else {
		printf("Please type 1 or 2.\n");
	}

	printf("\nPress Enter to exit...");
	getchar();
	return 0;
}
