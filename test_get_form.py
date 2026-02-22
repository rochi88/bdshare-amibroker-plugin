import requests
from bs4 import BeautifulSoup
import sys

res = requests.get("https://www.dsebd.org/day_end_archive.php", headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
soup = BeautifulSoup(res.text, "html.parser")
forms = soup.find_all("form")
for form in forms:
    print(f"\nForm action: {form.get('action')}")
    for inp in form.find_all(["input", "select"]):
        print(f"  {inp.get('name')} : {inp.get('type')}")
