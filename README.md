# VideoOptimizer

`VideoOptimizer` — проста Windows CLI-утиліта для пакетної оптимізації відеофайлів у папці. Вона знаходить відео, створює оптимізовані MP4-файли в окремій папці `_optimized_mp4` і показує, скільки місця вдалося зекономити.

Утиліта не реалізує власний відеокодек. Вона є C++-обгорткою над `ffmpeg.exe` та `ffprobe.exe`: C++ відповідає за пошук файлів, перевірки, безпечні імена результатів, запуск процесів і звіт.

## Що створюється

Вхідні формати:

- `.mp4`
- `.mov`
- `.mkv`
- `.avi`
- `.webm`
- `.m4v`

Вихідний формат завжди `.mp4`.

Приклад:

```powershell
input:  D:\Videos\test.mov
output: D:\Videos\_optimized_mp4\test.mp4
```

Якщо файл з таким іменем уже існує, буде створене безпечне ім'я:

```text
test_optimized.mp4
test_optimized_2.mp4
test_optimized_3.mp4
```

У режимі `--recursive` результати файлів із вкладених папок складаються в `_optimized_mp4` зі збереженням відносної структури підпапок.

## FFmpeg

Потрібні `ffmpeg.exe` і `ffprobe.exe`.

Утиліта шукає їх:

1. У `PATH`.
2. У підпапці біля `VideoOptimizer.exe`:

```text
tools\ffmpeg\bin\ffmpeg.exe
tools\ffmpeg\bin\ffprobe.exe
```

Якщо FFmpeg не знайдено, програма виведе:

```text
FFmpeg not found. Install FFmpeg or place ffmpeg.exe and ffprobe.exe into tools\ffmpeg\bin.
```

### Як встановити FFmpeg

Варіант 1: додати FFmpeg у `PATH`.

1. Завантажте Windows build FFmpeg з офіційного сайту або надійного дистрибутива.
2. Розпакуйте архів.
3. Додайте папку `bin`, де лежать `ffmpeg.exe` і `ffprobe.exe`, у системну змінну `PATH`.
4. Перевірте в PowerShell:

```powershell
ffmpeg -version
ffprobe -version
```

Варіант 2: покласти FFmpeg поруч з програмою:

```text
VideoOptimizer.exe
tools\ffmpeg\bin\ffmpeg.exe
tools\ffmpeg\bin\ffprobe.exe
```

## Збірка у Visual Studio 2022

1. Відкрийте `VideoConverter.sln` у Visual Studio 2022.
2. Виберіть конфігурацію `Release` і платформу `x64`.
3. Виконайте `Build Solution`.
4. Готовий файл буде в:

```text
bin\x64\Release\VideoOptimizer.exe
```

Також можна зібрати через Developer PowerShell for VS 2022:

```powershell
msbuild VideoConverter.sln /p:Configuration=Release /p:Platform=x64
```

## Приклади запуску

Запустити в поточній папці:

```powershell
.\VideoOptimizer.exe
```

Передати папку:

```powershell
.\VideoOptimizer.exe "D:\Videos"
```

Обробити вкладені папки:

```powershell
.\VideoOptimizer.exe "D:\Videos" --recursive
```

Задати параметри якості:

```powershell
.\VideoOptimizer.exe "D:\Videos" --crf 22 --preset slow --fps 30 --max-height 1080
```

Перезаписувати однакові вихідні файли:

```powershell
.\VideoOptimizer.exe "D:\Videos" --overwrite
```

Перевірити без запуску FFmpeg:

```powershell
.\VideoOptimizer.exe "D:\Videos" --dry-run
```

Рекомендований режим:

```powershell
.\VideoOptimizer.exe "D:\Videos" --recursive --crf 22 --preset slow --fps 30 --max-height 1080
```

## Параметри

- `--crf N` — якість H.264, за замовчуванням `22`.
- `--preset NAME` — preset x264, за замовчуванням `slow`.
- `--fps N` — FPS результату, за замовчуванням `30`.
- `--max-height N` — максимальна висота, за замовчуванням `1080`.
- `--audio-bitrate RATE` — бітрейт AAC, за замовчуванням `160k`.
- `--recursive` — обробляти вкладені папки.
- `--overwrite` — перезаписувати існуючий результат з базовим іменем.
- `--dry-run` — показати файли, майбутні результати та FFmpeg-команди без конвертації.
- `--help` — показати довідку.

## CRF

CRF керує якістю та розміром H.264:

- `18-20` — майже без втрати, більший файл.
- `21-23` — хороший баланс якості та розміру.
- `24-28` — сильніше стискання, можлива помітна втрата.

## Цільові параметри

Утиліта формує MP4 з такими параметрами:

- контейнер: MP4;
- відео: H.264 / `libx264`;
- pixel format: `yuv420p`;
- CRF: `22` за замовчуванням;
- preset: `slow` за замовчуванням;
- FPS: `30` за замовчуванням;
- максимальна висота: `1080` за замовчуванням;
- відео нижче заданої висоти не збільшується;
- аудіо: AAC, `160k`, `48000 Hz`, stereo;
- MP4 faststart: `-movflags +faststart`.

## Безпечна логіка

- Оригінальні файли не змінюються і не видаляються.
- Результат пишеться в `_optimized_mp4`.
- Файли, які вже лежать у `_optimized_mp4`, не обробляються повторно.
- Для тесту спочатку використовуйте `--dry-run`.
- Якщо результат не сподобався, оригінал залишається на місці.
- Якщо результат став більшим за оригінал, файл не видаляється автоматично, але це показується у статусі та summary.
