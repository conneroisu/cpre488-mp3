# CPRE 488 MP3

---

Autonomous Turret

---

## Dev Env

- Docker Powered Petalinux Tools
- Expect Scripts are aweful

---

## Cleaning

```bash
petalinux-build -x mrproper -f
```

---

## Sizing FSBL

- Inline 
- Reuse Status int (aka `int Status;`)