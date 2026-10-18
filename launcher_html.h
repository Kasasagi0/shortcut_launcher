#ifndef LAUNCHER_HTML_H
#define LAUNCHER_HTML_H

#include <stdio.h>

/*
  Writes the launcher page.

  hasIcon      : 1 if shortcut_icon\<id>.png was made for this game
  id           : the steam game id (digits)
  delayMs      : how long the page waits before closing after launching (0 = instantly)
  setupDelayMs : on the FIRST visit the page does not launch right away. It looks
                 for its icon file, adds it to the page as the page icon, and waits
                 this long (so the browser can save it), THEN launches the game.

  The page does not assume where the icon is hosted. It checks, in this order:
    ../shortcut_icon/<id>.png   (page inside a shortcut_html folder)
    shortcut_icon/<id>.png      (page and the shortcut_icon folder side by side)
    <id>.png                    (everything in one folder)
    ../<id>.png                 (page in a sub folder, pngs one level up)
  If none exist it says which addresses it tried, waits 4 seconds, and launches anyway.
  Opening the page with ?setup on the end repeats the first-visit steps.
*/
static void write_launcher_html(FILE* out, int hasIcon, const char* id,
	int delayMs, int setupDelayMs) {
	fprintf(out,
		"<!DOCTYPE html>\n"
		"<html style=\"background:#000\">\n"
		"<head>\n"
		"  <meta charset=\"utf-8\">\n"
		"  <meta name=\"color-scheme\" content=\"dark\">\n"
		"  <meta name=\"theme-color\" content=\"#000000\">\n"
		"  <title>Launching...</title>\n"
		"  <style>\n"
		"    html, body { background:#000; margin:0; height:100%%; }\n"
		"    body { color:#888; font:14px sans-serif; padding:16px; box-sizing:border-box; }\n"
		"    #msg { white-space:pre-line; }\n"
		"  </style>\n"
		"</head>\n"
		"<body style=\"background:#000\">\n"
		"<div id=\"msg\"></div>\n"
		"<script>\n"
		"  var id = \"%s\";\n"
		"  var hasIcon = %d;\n"
		"  var setup = location.search.indexOf(\"setup\") >= 0;\n"
		"  var seen = false;\n"
		"  try { seen = !!localStorage.getItem(\"seen4_\" + id); } catch (e) {}\n"
		"\n"
		"  function say(t) { document.getElementById(\"msg\").textContent = t; }\n"
		"  function leave() { window.close(); history.back(); }\n"
		"  function launch() {\n"
		"    location.href = \"steam://rungameid/\" + id;\n"
		"    setTimeout(leave, %d);\n"
		"  }\n"
		"\n"
		"  var names = [\n"
		"    \"../shortcut_icon/\" + id + \".png\",\n"
		"    \"shortcut_icon/\" + id + \".png\",\n"
		"    id + \".png\",\n"
		"    \"../\" + id + \".png\"\n"
		"  ];\n"
		"  var tried = [];\n"
		"\n"
		"  function findIcon(i) {\n"
		"    if (i >= names.length) {\n"
		"      say(\"Icon file not found. Tried:\\n\" + tried.join(\"\\n\") +\n"
		"          \"\\n\\nUpload the shortcut_icon folder (or the png files) to the same repository.\\n\" +\n"
		"          \"Launching the game anyway...\");\n"
		"      setTimeout(launch, 4000);\n"
		"      return;\n"
		"    }\n"
		"    var img = new Image();\n"
		"    img.onload = function () {\n"
		"      var link = document.createElement(\"link\");\n"
		"      link.rel = \"icon\";\n"
		"      link.type = \"image/png\";\n"
		"      link.href = img.src;\n"
		"      document.head.appendChild(link);\n"
		"      try { localStorage.setItem(\"seen4_\" + id, \"1\"); } catch (e) {}\n"
		"      setTimeout(launch, %d);\n"
		"    };\n"
		"    img.onerror = function () { tried.push(img.src); findIcon(i + 1); };\n"
		"    img.src = names[i];\n"
		"  }\n"
		"\n"
		"  if (!hasIcon || (seen && !setup)) {\n"
		"    launch();\n"
		"  } else {\n"
		"    say(\"Saving icon...\");\n"
		"    findIcon(0);\n"
		"  }\n"
		"</script>\n"
		"</body>\n"
		"</html>\n",
		id, hasIcon ? 1 : 0, delayMs, setupDelayMs);
}

#endif
