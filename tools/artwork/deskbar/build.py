#!/usr/bin/env python3
"""The folder icons of the Deskbar's Applications menu, in the Prose fork's
folders by category (build/jam/DeskbarCategories).

Each is the folder the Deskbar draws its menus with (Haiku's virtual folder,
data/artwork/icons/Folder_virtual) with an emblem in the corner where Haiku's
own special folders carry theirs (Folder_development, Folder_fonts, ...):
one of Haiku's folder overlays as it is, a Haiku icon made small, or an
emblem drawn here in SVG. The Deskbar draws its menus at 16 pixels, where an
emblem that size is a few specks, so up to 21 pixels a second, larger copy
of the emblem is drawn instead -- the size at which Folder_virtual itself
switches to its thicker outline (level of detail, data-hvif-lod).

Writes
  <tree>/src/data/directory_attrs/deskbar-applications-<category>.rdef
        the icon as the folder's BEOS:ICON attribute: build/jam/packages/Haiku
        gives it to the haiku package's folders, and prosepkg reads it for
        the folders it makes in the ports' packages
  tools/artwork/deskbar/<Category>.svg    each icon as composed, to look at
  tools/artwork/deskbar/preview.png       all of them at 16, 32 and 64 pixels

usage: build.py [haiku tree]      (default /Volumes/HaikuSrc/haiku)
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import hviftool as h

# Where Haiku's folder overlays sit on the 64 unit grid (they span about
# x 4..29, y 26..62): an icon made small goes in this box
EMBLEM_BOX = (3.0, 31.0, 31.0, 62.0)
# up to 21 pixels (Folder_virtual's own switch), the emblem fills this one
SMALL_SIZES_BOX = (0.0, 20.0, 42.0, 64.0)
SMALL_SIZES = 21 / 63.75

# (folder, kind, source, what it shows)
CATEGORIES = [
    ('Accessories', 'icon', 'App_ArmyKnife', 'a pocket knife (App_ArmyKnife)'),
    ('Development', 'overlay', 'Overlay_development', 'the bug (Overlay_development)'),
    ('Files', 'icon', 'Tracker_copy', "Tracker's copying papers (Tracker_copy)"),
    ('Games', 'icon', 'Prefs_Joystick', 'a joystick (Prefs_Joystick)'),
    ('Graphics', 'icon', 'App_ImageEditor', 'a palette and brush (App_ImageEditor)'),
    ('Internet', 'icon', 'Misc_Earth', 'the globe (Misc_Earth)'),
    ('Multimedia', 'svg', 'music.svg', 'two notes, drawn for it (music.svg)'),
    ('Office', 'overlay', 'Overlay_document', 'a page (Overlay_document)'),
    ('System', 'overlay', 'Overlay_gear', 'a gear (Overlay_gear)'),
]

RDEF = '''/*
 * The folder icon of "%(name)s" in the Deskbar's Applications menu
 * (build/jam/DeskbarCategories): the Deskbar's folder with %(what)s.
 * Made from Haiku's artwork (data/artwork/icons) by HaikuArmQemu's
 * tools/artwork/deskbar/build.py; the SVG it composed is there too.
 */

%(resource)s'''


def main():
    tree = sys.argv[1] if len(sys.argv) > 1 else '/Volumes/HaikuSrc/haiku'
    artwork = os.path.join(tree, 'data', 'artwork', 'icons')
    attrs = os.path.join(tree, 'src', 'data', 'directory_attrs')
    base = h.decode(h.load(os.path.join(artwork, 'Folder_virtual')))
    icons = []
    large = '%s 4' % h.fmt(SMALL_SIZES)
    small = '0 %s' % h.fmt(SMALL_SIZES)
    for name, kind, source, what in CATEGORIES:
        if kind == 'svg':
            emblem = h.svg_to_icon(os.path.join(HERE, source))
        else:
            emblem = h.decode(h.load(os.path.join(artwork, source)))
        # overlays and the SVG ones are drawn in place; icons are made small
        placed = h.fit(emblem, EMBLEM_BOX) if kind == 'icon' else None
        svg_path = os.path.join(HERE, name + '.svg')
        with open(svg_path, 'w') as f:
            f.write(h.compose([(base, None, None), (emblem, placed, large),
                (emblem, h.fit(emblem, SMALL_SIZES_BOX), small)]))
        data = h.encode(h.svg_to_icon(svg_path))
        rdef = os.path.join(attrs, 'deskbar-applications-%s.rdef' % name.lower())
        with open(rdef, 'w') as f:
            f.write(RDEF % {'name': name, 'what': what, 'resource': h.rdef_resource(data)})
        icons.append(h.decode(data))
        print('%-12s %4d bytes  %s' % (name, len(data), os.path.relpath(rdef, tree)))
    with open(os.path.join(HERE, 'preview.png'), 'wb') as f:
        f.write(h.sheet(icons, [16, 32, 64], [4, 2, 1], b'\xd8\xd8\xd8\xff', 3))


if __name__ == '__main__':
    main()
