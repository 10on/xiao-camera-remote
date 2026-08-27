repo: 10on/xiao-camera-remote
branch: master

## Last sync
date: 2026-08-27T21:10:00Z

### Updated in this project
- Recreated the 9 screen states the firmware renders today, at real panel size (280×240)
- Drew the new UX model from `uploads/pult-ux-redesign-requirements.md`: конфигурации вместо Device List, экран подключения, экраны Main (Slider/Dolly), дубль, ошибки, редактор, настройки
- Kept the existing palette/geometry from `src/theme.h` and the Adafruit_GFX limits (no gradients, 6×8 chrome font)

## Screen map
| Project screen | Repo files |
| --- | --- |
| Пульт — экраны сейчас.dc.html | src/menu.cpp, src/menu.h, src/theme.h, src/profile.cpp, docs/screen-design.md, docs/design/Remote Screen.dc.html |
| Пульт — новая модель.dc.html | src/theme.h, src/command.h, src/profile.h, src/config.h, docs/screen-design.md |
