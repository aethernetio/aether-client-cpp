# Телеметрия: результаты проверки

Проверены два продукта: **AppTraverse App** и **AeroAdmin Chat**. Обычные примеры и тесты исключены из производственных каталогов; `aeroadmin/examples/aeroadmin_chat` включён явно как настоящий продукт.

Реальные `.aetlog` обоих продуктов переданы через облачную инфраструктуру Aether выделенному клиенту-хранителю. После перезапуска отправителя и хранителя UID сохранились; получены durable ACK, файлы совпали с исходными побайтно и успешно разобраны соответствующими оффлайн-парсерами. Чужой продукт каждым парсером отклонён.

## Размер записи

Измерения ниже относятся к финальным профилированным C++-артефактам. Исключено только содержимое UTF-8 payload; префикс длины остаётся. Метрики и внешний JSON-конверт в стоимость записи не включены. Последний столбец дополнительно распределяет по строкам заголовки блоков/потоков и временные якоря.

| Продукт / namespace | Строк | Raw, байт | Payload, байт | Запись без payload, Б | С framing, Б |
|---|---:|---:|---:|---:|---:|
| apptraverse-app / aether | 2 | 47 | 0 | 4.000 | 11.000 |
| apptraverse-app / apptraverse | 1 | 76 | 38 | 4.000 | 18.000 |
| apptraverse-app / objects | 158 | 11970 | 11192 | 4.006 | 4.424 |
| apptraverse-app / product.apptraverse-app | 442 | 15428 | 13512 | 4.000 | 4.186 |
| aeroadmin-chat / aether | 2 | 47 | 0 | 4.000 | 11.000 |
| aeroadmin-chat / apptraverse | 83 | 3359 | 2901 | 4.578 | 4.783 |
| aeroadmin-chat / objects | 2403 | 109678 | 99371 | 4.053 | 4.250 |
| aeroadmin-chat / product.aeroadmin-chat | 41 | 1831 | 1579 | 4.976 | 5.390 |

Полная разбивка метрик, payload, якорей, framing и внешнего артефакта сохранена в [report.json](report.json) и `native-*/bytes.json`. Типичная запись занимает около **4 байт без payload**. Малые выборки в 1–2 строки дают заметную долю заголовков.

Среднее по всем namespace: **4.002 Б** для App и **4.085 Б** для Chat; с заголовками лог-блоков — **4.294 / 4.291 Б**. Если дополнительно распределить по строкам все пакеты метрик, получится **4.609 / 4.375 Б** соответственно. Внешний manifest и сетевые пакеты в эти числа не входят.

## Удалён шум успешных RAM-сохранений

Одинаковый сценарий чата без SDK: **23,842 → 2,396 строк**, **1,585,731 → 104,042 raw-байт**; сокращение **93.44%**.

`kRamSaved`: 21 446 форматированных логов заменены счётчиком. Его итог остался **21 446**, новых строк этого тега **0**, duration samples **0**. Все остальные количества событий и метрик совпали. Сообщения AppTraverse/продукта совпали точно; в объектных сообщениях отличаются случайные ID свежих графов. После нормализации только этих ID сообщения совпадают; исходные строки сохранены.

## Частоты и таблицы тегов

**4 687** реальных строк в замороженном профиле: 3 132 из продуктовых сценариев и 1 555 из отдельного реального SDK relay fixture. Последний — сетевой тест библиотеки, а не пользовательский сценарий чата. Кумулятивный journal учтён один раз.

Полный SDK-каталог содержит **483** производственных места логирования. Ранжирование даёт **1 285 байт экономии тегов** на замороженной SDK-выборке относительно исправленной таблицы по умолчанию. Это одна общая SDK-экономия; суммировать её дважды по продуктам нельзя. В новом реальном relay-прогоне все **326** наблюдавшихся SDK-строк используют однобайтный тег, неизвестных/fallback ID **0**. Общий размер сетевых прогонов напрямую не сравнивался: нагрузка различается.

Объектные 21 ID сохранены. AppTraverse имеет 5 мест в закреплённой версии App и 13 в версии Chat; продуктовые слои — 13 и 8. Их перестановка не даёт экономии, поэтому ID сохранены. Числа растут по диапазонам 1–239 / 240–4095 / далее: 1 / 2 / 4 байта.

Это диапазоны индекса **тега**. Значения метрик используют отдельный `TieredInt<uint8_t, 200, 5000, 100000000>` с представлением в 1 / 2 / 4 / 8 байт.

Оба продукта используют один файл профиля. Частоты разделены по namespace и commit: разные закреплённые версии AppTraverse не смешиваются. Профиль воспроизведён независимо с тем же SHA256:

`89f3e0c4bdaf41bd081ed11b2838274a4eb38d0ee3fe131ecd2c0f167fc4a328`

В сетевом тесте выявлены и исправлены пропуски `BW_LOG_*` и обычного `AE_TELE[D]` в SDK-сканере. Старый fixture разобран по отдельной проверенной реконструкции четырёх FNV ID; исходная таблица сохранена. Финальный прогон полностью разбирается непосредственно по новой сгенерированной таблице.

## Перезапуск и несовместимость

Два отдельных процесса на одном реальном файловом профиле: Chat **41 → 82**, App **442 → 884** продуктовых событий. Pending batches **1 → 2**, старый batch не изменился побайтно, появился отдельный новый delta-batch. Повтор старого front не суммировался второй раз.

После смены несовместимой таблицы SDK реально наблюдался сброс старого telemetry state, при этом UID клиента сохранился. Совместимость определяется отдельным schema fingerprint; точный каталог/commit/dirty tree остаётся в provenance каждого артефакта.

Финальные продуктовые снимки после профилирования сохранили все количества логов и метрик. Сообщения совпали с указанной выше нормализацией случайных объектных ID; время и длительность не сравнивались как детерминированные значения.

## Проверки и воспроизведение

Python pipeline **13/13**, raw core **17/17**, финальный SDK CTest **10/10**; выбранные продуктовые тесты App **16/16** (два посторонних live integration-теста исключены), Chat **3/3**. Выполнение — Windows x64; продуктовые бинарники Debug. Другие платформы не запускались. Точные каталоги, embedded manifest, CMakeCache и Native ExportMap сохранены рядом с каждым снимком. Поле configuration в текущем manifest пустое, поэтому оно не выдаётся за полный набор компиляторных флагов.

Парсинг и профилирование после получения файлов работают без сети:

```powershell
python aether-tele/tools/decode_apptraverse_app.py telemetry-evidence/apptraverse-app/native-profiled/capture-native.aetlog --catalogs telemetry-evidence/catalogs --output app-records.jsonl --summary app-bytes.json
python aether-tele/tools/decode_aeroadmin_chat.py telemetry-evidence/aeroadmin-chat/native-profiled/capture-native.aetlog --catalogs telemetry-evidence/catalogs --output chat-records.jsonl --summary chat-bytes.json
python aether-tele/tools/profile_artifacts.py telemetry-evidence/apptraverse-app/native-baseline/capture-native.aetlog telemetry-evidence/aeroadmin-chat/native-baseline/capture-native.aetlog --catalogs telemetry-evidence/catalogs --output profile-products.json
python telemetry-evidence/merge_sdk_profile.py --input profile-products.json --output frequency-reproduced.json
python aether-tele/tools/test_offline_pipeline.py
```

Формат, CMake API и команды генерации: [инструкция инструментов](../aether-tele/tools/TELEMETRY.md). Машинные доказательства: `cleanup/comparison.json`, `profile-comparison.json`, `restart/comparison.json`, `relay-acceptance-6600a016/offline-product-validation.json`, `sdk-ranked-validation.json`.

## Запуск зарегистрированного хранителя

UID: `96b82bf8-9ee2-4bd4-8fc9-49a2049d712f`. Все ограниченные тестовые процессы завершены. [Скрипт запуска](relay-acceptance-6600a016/start-keeper.ps1) использует сохранённый профиль этого клиента. [Настройка двух продуктов](relay-acceptance-6600a016/set-product-keeper.ps1) задаёт UID только в текущей PowerShell-оболочке:

```powershell
./telemetry-evidence/relay-acceptance-6600a016/start-keeper.ps1
# В другой оболочке, перед запуском приложений:
. ./telemetry-evidence/relay-acceptance-6600a016/set-product-keeper.ps1
```

App собирается с явно выбранным `apptraverse-product-compat` на закреплённом `bab6312`, Chat — с текущим AppTraverse `fb9a7f1`. Точные команды приведены в [App README](../apptraverse-app/telemetry/README.md) и [Chat README](../aeroadmin/examples/aeroadmin_chat/telemetry/README.md). Постоянный production или явно заданный профиль сохраняет состояние; намеренно свежий debug-профиль начинает его заново. Изменения находятся в локальных рабочих копиях; обычные удалённые зависимости потребуют согласованной публикации новых версий.


Original evidence paths refer to the validation workspace. Private client profiles and keys are excluded from Git.
