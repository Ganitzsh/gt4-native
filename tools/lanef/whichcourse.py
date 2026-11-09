# whichcourse.py SHOT.png: the selected city course, from the "Course length / Elevation change" line at the bottom
import sys, os
from PIL import Image, ImageChops, ImageStat
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'info')
shot = Image.open(sys.argv[1]).convert('RGB').resize((1280, 720)).crop((300, 668, 980, 708)).convert('L')
scores = sorted((ImageStat.Stat(ImageChops.difference(shot, Image.open(os.path.join(here, f)))).mean[0], f[:-4])
                for f in os.listdir(here))
ok = scores[0][0] < 12 and scores[1][0] - scores[0][0] > 2
print(scores[0][1] if ok else 'unknown', ' '.join(f'{n}={d:.1f}' for d, n in scores))
