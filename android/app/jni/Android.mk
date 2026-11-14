# SDL2, SDL2_image (con stb_image) y SDL2_ttf (con FreeType, sin HarfBuzz):
# las descarga preparar.sh en esta carpeta.
SUPPORT_HARFBUZZ := false
USE_STBIMAGE := true
include $(call all-subdir-makefiles)
