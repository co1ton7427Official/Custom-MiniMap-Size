import urllib.request
import ssl

ctx = ssl.create_default_context()


def probe(name, url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "probe/1.0"})
        with urllib.request.urlopen(req, timeout=30, context=ctx) as r:
            data = r.read(400)
        print("OK   %s: %s %dB" % (name, r.status, len(data)))
        return True
    except Exception as e:
        print("FAIL %s: %s: %s" % (name, type(e).__name__, e))
        return False


for name, url in [
    ("github", "https://github.com"),
    ("gh-objects", "https://objects.githubusercontent.com"),
    ("sourceforge", "https://sourceforge.net"),
    ("winlibs", "https://winlibs.com"),
    ("pypi-ziglang", "https://pypi.org/simple/ziglang/"),
    ("msys2", "https://mirror.msys2.org"),
    ("nuget", "https://api.nuget.org/v3/index.json"),
]:
    probe(name, url)
