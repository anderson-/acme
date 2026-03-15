from zeroconf import Zeroconf, ServiceBrowser
import time

results = []

class Listener:
    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        if info:
            ip = ".".join(str(b) for b in info.addresses[0])
            results.append((ip, info.port))

    def update_service(self, zc, type_, name):
        pass

zc = Zeroconf()
ServiceBrowser(zc, "_arduino._tcp.local.", Listener())
time.sleep(2)
zc.close()

for ip, port in results:
    print(f"{ip}:{port}")