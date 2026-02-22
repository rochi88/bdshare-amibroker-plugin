import requests
url = 'https://www.dsebd.org/day_end_archive.php'
payload = {'startDate': '2024-02-22', 'endDate': '2026-02-22', 'inst': 'BATBC', 'archive': 'data'}
headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'}
res = requests.post(url, data=payload, headers=headers)
print(res.status_code)
with open('d:/software/dsebd_hist_response.html', 'w', encoding='utf-8') as f:
    f.write(res.text)
