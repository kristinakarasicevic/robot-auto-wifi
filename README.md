# Robot Autic

Diferencijalno vozilo kojim se upravlja iz aplikacije preko WiFi-a (napred, nazad, levo, desno). U aplikaciji se vidi uzivo prenos sa kamere koja stoji na vozilu i udaljenost od prepreka koju meri ultrazvucni senzor. Senzor sluzi da autic ne udari u prepreku iako ga korisnik tako navodi (jos nije odluceno da li ce da koci ili samo da salje upozorenje). Aplikacija se koristi sa telefona.

## Komponente

ESP32-S3 kao glavni kontroler (motori, senzor, komande), ESP32-CAM za video, L298N drajver za motore, ultrazvucni senzor za udaljenost, LM2596 step-down za regulaciju na 5V, baterije, protoploca i jumperi.

## Arhitektura

Sistem cine tri nezavisne celine koje su sve na istoj WiFi mrezi (hotspot telefona). Glavni kontroler (ESP32-S3) prima komande iz aplikacije, upravlja motorima i salje izmerenu udaljenost. Kamera (ESP32-CAM) salje sliku direktno aplikaciji i potpuno je odvojena od kontrolera, bez podatkovne veze i bez zajednickog napajanja. Aplikacija na telefonu prikazuje sliku i udaljenost, a salje komande vozilu.

## Pin mapping (ESP32-S3)

Motori preko L298N: levi IN1=4, IN2=5, EN=15; desni IN1=6, IN2=7, EN=16. Ultrazvucni senzor: TRIG=17, ECHO=18. LED=21.

Na ECHO pin obavezno ide voltage divider (1k/2k) jer senzor daje 5V, a GPIO trpi 3.3V. Zajednicki GND izmedju ESP32, L298N i baterija je obavezan.

## Napajanje

Baterije idu na LM2596 podesen na 5V, pa na ESP32. Pri startu motora napon moze da propadne i da resetuje ESP32 (brownout). Resenje je kondenzator 470-1000uF na 5V/GND i litijumske baterije.

## Pokretanje

Potrebni su Arduino IDE (sa podrskom za ESP32-S3 i ESP32-CAM), VS Code sa Live Server ekstenzijom i telefonski hotspot kao zajednicka mreza.

Za glavni kontroler otvori robot_auto_kontroler.ino, u Tools ukljuci USB CDC On Boot = Enabled i flesuj. Za kameru otvori robot_kamera_stream.ino i flesuj; stream je na http://<IP-kamere>:81/stream. Aplikaciju pokreni tako sto robot-auto-kontrola.html otvoris preko Live Servera, uz telefon i PC na istom hotspotu.

## Struktura

    robot_auto_kontroler/   firmware ESP32-S3
    robot_kamera_stream/    firmware ESP32-CAM
    sajt/                   web aplikacija (html, css, js)
    testTockovi/            test skica za motore

## WiFi kredencijali

SSID i sifra ne idu na git. U svakom firmware folderu stoji secrets.h (naveden u .gitignore) sa dva reda:

    #define WIFI_SSID "tvoj_ssid"
    #define WIFI_PASS "tvoja_sifra"

Na git ide samo secrets.example.h sa praznim vrednostima, kao sablon.

## Status

Sasija sa motorima, drajverom, step-down-om i kontrolerom je sklopljena i testirana (motori su spojeni tako da su prednji i zadnji desni zajedno, isto i levi). Ultrazvucni senzor je povezan ali jos nije testiran. Sledi testiranje senzora, pa povezivanje i testiranje kamere.

Jos nije odluceno da li ce aplikacija biti web ili Android, i da li senzor koci ili samo salje upozorenje.
