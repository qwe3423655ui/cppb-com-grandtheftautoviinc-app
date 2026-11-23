Grand Theft Auto VI (Claude edition) (com.grandtheftautoviinc.app)

Как получить APK:
1. Создайте пустой репозиторий на github.com и загрузите в него ВСЁ содержимое этого архива (вместе с папкой .github).
2. Откройте вкладку Actions → Build APK → дождитесь зелёной галочки (2–5 минут).
3. Внутри запуска скачайте артефакт app-debug-apk — в нём app-debug.apk.
4. Установите APK на телефон (разрешите установку из неизвестных источников).

C++: приложение работает как NativeActivity — вместо main() нужна точка входа void android_main(struct android_app* app) (подключите <android_native_app_glue.h>).
