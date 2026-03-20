# YouTube-Cloude
---
## Вам понадобится установить зависимости
><br/> cv2
<br/> numpy as np
<br/> os
<br/> math
<br/> subprocess
<br/> tempfile
<br/> shutil
<br/> sys
<br/> re
<br/> hashlib
---
# Для кодирования любого файла:
><br/><code>python coder.py encode FILENAME.xxx FILENAME.mp4</code>
<br/>Где .xxx - это расширение вашего файла, который находится в одной папке с coder.py
---
# Для декодирования видеофайла:
><br/><code>python coder.py decode FILENAME.mp4</code>
---
## Ключ шифрования
><br/>Вы можете создать в папке с coder.py файл <code>key.txt</code> и написать внутри него ключ шифрования любой длины. После этого код будет учитывать присутствие key.txt при кодирование файлов и в дальнейшем такие видеофайлы обратно декодироваться правильно будуту только с правильным ключом, указанном в key.txt.
