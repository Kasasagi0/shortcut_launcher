ShortcutMaker - turns Steam .url shortcuts into launcher .html files

Keep these next to each other (you can move the whole folder anywhere):
  ShortcutMaker.exe
  shortcut_html\   (generated html files appear here)
  shortcut_icon\   (<id>.ico = copy of the Steam icon, <id>.png = small icon for the page)

Run ShortcutMaker.exe, type 1 (one shortcut) or 2 (whole folder), pick it.
No installer or extra files needed. Works on 64-bit Windows.
Note: the original Steam icon has to exist on the PC when you convert.

Hosting (GitHub Pages):
  Upload the .html files AND the .png files (the shortcut_icon folder).
  The page finds its icon by itself, it checks these places in order:
    ../shortcut_icon/<id>.png   shortcut_icon/<id>.png   <id>.png   ../<id>.png
  so it does not matter if the html is in the repository root or in a sub folder.

How the launcher pages behave:
  FIRST time a page is opened in your browser: it shows "Saving icon...", finds its
  icon, adds it to the page, waits about 3 seconds so the browser can save it, THEN
  launches the game. Every later time: launches and closes instantly.
  If the icon can't be found anywhere, it shows the addresses it tried, waits 4
  seconds and launches the game anyway (and tries again next time).
  To repeat the first-time steps, add ?setup to the end of the page's address.
  Settings are at the top of main.c: CLOSE_DELAY_MS (0) and SETUP_DELAY_MS (3000).
