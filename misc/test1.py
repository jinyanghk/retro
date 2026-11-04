import json
from collections import Counter

with open('d1_town_05.json') as f:
    data = json.load(f)

mats = []
for b in data['brushes']:
    for f in b['faces']:
        mats.append(f.get('material', '').lower())

for mat, count in Counter(mats).most_common(20):
    print(f"{count:5d}  {mat}")