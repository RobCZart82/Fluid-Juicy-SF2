## Telepítési útmutató

Fluid Juicy SF2 | Magyar | 2026. november 15.

## Kezdés előtt

Fejlesztői előzetes: stabil v1.0.0 kiadás még nincs. A macOS PKG készítése fejlesztés alatt áll; a Windows EXE tervezett. Az alábbiak a tervezett telepítési folyamatot írják le, nem a befejezett telepítési tesztek eredményét.

## Letöltés és bizalom

Kizárólag a github.com/RobCZart82/Fluid-Juicy-SF2 Releases oldaláról vagy a fejlesztő által hivatkozott tesztcsomagból tölts le. Apple Siliconhoz arm64, Intel Machez x86_64, Windowshoz x64 változat kell. A telepítők tervezett tartalma VST3 és standalone. SoundFont nincs mellékelve. A megfelelő forráscsomagot és licencanyagokat is őrizd meg.

## Miért jelenik meg figyelmeztetés?

A telepítőknek nincs ellenőrzött kiadói aláírásuk. A macOS programok helyi, ad-hoc aláírása nem igazolja a fejlesztő személyét; Apple-notarizáció nincs. A Windows programok aláíratlanok. Az ellenőrzőösszeg a közzétett értékhez képest észleli az eltérést, de nem bizonyítja a letöltés biztonságosságát.

## macOS: a telepítő engedélyezése

1. Zárd be a REAPER-t és más hangprogramokat. Nyisd meg a letöltött PKG-t.
2. Ha a macOS az ellenőrizetlen fejlesztő miatt blokkolja, zárd be a figyelmeztetést.
3. Nyisd meg: Rendszerbeállítások > Adatvédelem és biztonság. A Biztonság résznél keresd a blokkolt fájlt, majd a Megnyitás mindenképpen / Open Anyway lehetőséget, ha elérhető.
4. Ellenőrizd a fájl nevét; csak megbízható letöltést engedélyezz. Kövesd a Telepítő lépéseit; a rendszerszintű telepítéshez rendszergazda kell. Régebbi macOS-en: Rendszerbeállítások > Biztonság és adatvédelem > Általános. A feliratok verziónként eltérhetnek.

## macOS: indítás és keresés

Tervezett helyek: /Applications/Fluid_Juicy_SF2.app és /Library/Audio/Plug-Ins/VST3/Fluid_Juicy_SF2.vst3. Az alkalmazást az Alkalmazások mappából indítsd. Első indításkor külön kivételre is szükség lehet. Indítsd újra a REAPER-t, szükség esetén keresd újra a VST3 plugineket. Név: Fluid Juicy SF2 (RobCZart82). Ne maradjon régi másolat a ~/Library/Audio/Plug-Ins/VST3/ mappában.

## Ha a macOS továbbra is blokkol

Plugin-kereséskor is megjelenhet tiltás: zárd be az üzenetet, nézd meg az adott fájlt az Adatvédelem és biztonság résznél, és csak elérhető kivétel után ismételd a keresést. Kártevőre vagy sérült fájlra utaló jelzésnél, illetve kivétel hiányában állj meg, és jelezd a pontos üzenetet. Ne kapcsold ki globálisan a Gatekeepert, a SIP-et vagy a vírusvédelmet. Terminálos karanténtörlés nem szükséges ehhez az útmutatóhoz.

## Windows telepítés

Windows x64 | tervezett EXE-folyamat

## SmartScreen és rendszergazdai kérdés

1. Zárd be a REAPER-t és más hangprogramokat; indítsd el a letöltött EXE-t.
2. A Microsoft Defender SmartScreen Windows protected your PC jelzésénél válaszd a További információ / More info lehetőséget. Ha van Futtatás mindenképpen / Run anyway, csak a fájlnév és a megbízható forrás ellenőrzése után használd.
3. A felhasználói fiókok felügyelete Ismeretlen kiadót jelezhet. Csak a várt telepítő számára engedélyezd a folytatást.
4. Kövesd a telepítőt; VST3 és standalone is kerüljön fel. A Windows-telepítő megvalósítása és pontos választási lehetőségei még hátravannak.

## Ha nincs folytatási lehetőség

A Smart App Control, rendszergazdai házirend, Windows S mód vagy kártevővédelem megakadályozhatja a futtatást. Nem mindig van Run anyway gomb. A telepítéshez ne kapcsold ki a Defendert, a SmartScreent vagy a Smart App Controlt; kérj segítséget a gép rendszergazdájától, vagy jelezd a tiltást. A kártevőriasztás nem azonos az ismeretlen kiadó jelzésével.

## Telepítés után

A VST3 tervezett helye: C:\Program Files\Common Files\VST3\Fluid_Juicy_SF2.vst3. A standalone helye és parancsikonjai a Windows-telepítővel együtt véglegesülnek. Indítsd újra a REAPER-t, és keresd újra a VST3 plugineket. Ha Visual C++ futtatókörnyezet kell, kizárólag a Microsoft hivatalos x64 csomagját használd; ne tölts le külön DLL-eket ismeretlen oldalakról.

## Az első hang

A Browse vagy Banks segítségével tölts be saját SF2/SF3 bankot. A standalone-ban válassz hang- és MIDI-eszközt, vagy töltsd be a VST3-at a REAPER-be. Tizenhat szólamhoz az 1-16 MIDI-csatornát egyetlen példányra küldd, 16-part MIDI módban. Alacsony lehallgatási hangerővel kezdj. Mentsd és nyisd vissza a projektet a bank-visszatöltés ellenőrzéséhez.

## Hibajelzés és korlátok

Add meg az operációs rendszert, processzorarchitektúrát, REAPER-verziót, pontos buildet/commitot és a teljes hibaüzenetet a repositoryban. Jelszót és privát fájlt ne küldj. A tiszta gépes telepítés, frissítés és régebbi macOS-ek támogatása még tesztelendő. Nem garantálható az aláíratlan szoftver futása minden felügyelt gépen.

## Hivatalos források

Apple: https://support.apple.com/en-us/102445
Microsoft: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
Projekt: https://github.com/RobCZart82/Fluid-Juicy-SF2
