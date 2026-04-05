# Arkhitektura i dalshee razvitie

## Chto sdelano seichas

- Vydeleno nezavisimoe C++-yadro s klassami `YouTubeEncoder` i `YouTubeDecoder`.
- CLI yavlyaetsya tonkoi obvyazkoi nad yadrom.
- Video-vvod/vyvod vypolnyaetsya cherez `ffmpeg`, poetomu yadro ne zavisitsya ot GUI i OpenCV.

## Kak obychno gotovyat proekt pod budushchii GUI

1. Delyat proekt na sloi:
   - `core` - algoritmy, modeli dannykh, rabota s failami.
   - `app/cli` - komandnaya stroka.
   - `app/gui` - budushchii Qt-interfeis.
2. Vynosyat progress/logging v abstraktsii:
   - seichas dlya etogo est `ProgressSink`.
   - v Qt ego udobno zamenit adapterom na `signals/slots`.
3. Ne smeshivayut logiku i otrisovku:
   - Qt dolzhen vyzyvat metody yadra, a ne khranit algoritm v vidzhetakh.
4. Zakladyvayut rasshiryaemye DTO i konfiguratsiyu:
   - pozhe legko dobavit vybory FPS, razresheniya, klucha i katalogov output.

## Podkhody dlya Qt-chasti

- Variant 1: `QMainWindow` + rabotnik v `QThread`.
  Podkhod khorosh dlya klassicheskogo desktop-prilozheniya.
- Variant 2: Qt Widgets dlya bystrogo utilitarnogo interfeisa.
  Proshche vsego dlya etogo proekta.
- Variant 3: Qt Quick / QML.
  Podkhod udoben, esli nuzhen bolee sovremennyi interfeis i animatsiya.

## Gde optimizirovat dalshe

- Pipeline "chtenie faila -> generatsiya kadrov -> ffmpeg" mozhno raspallelit po etapam.
- Dekodirovanie mozhno uskorit packet-based obrabotkoi i parallel sample-analizom kadra.
- VMesto nakopleniya vsekh blokov v pamyati mozhno sdelat potokovoe vosstanovlenie baitov.
- Pri perekhode na GUI stoit zapuskat encode/decode v fonovom potoke s signalami progressa.
