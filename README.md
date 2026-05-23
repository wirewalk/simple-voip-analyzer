# simple-voip-analyzer

Монитор RTP-потоков в реальном времени.

## Что делает

- Захватывает UDP-пакеты через libpcap
- Разбирает RTP-заголовки (RFC 3550)
- Группирует пакеты по SSRC (отдельные потоки)
- Считает статистику для каждого потока:
  - Jitter (RFC 3550, Appendix A.8)
  - Packet loss (по gap в sequence numbers)
  - Reordered packets
  - Duplicate packets
  - MOS-оценка (упрощённая E-model)
- Выводит таблицу потоков в реальном времени

## Сборка

```bash
make            # бинарник voip-analyzer
make test       # сборка + запуск тестов
```

Требуется libpcap-dev. Если libpcap недоступна - соберётся с заглушкой (захват отключён).

## Запуск

```bash
sudo ./voip-analyzer          # слушать все интерфейсы
sudo ./voip-analyzer eth0     # слушать конкретный интерфейс
```

Нужны root-права для захвака пакетов.

## Формат вывода

```
=== RTP Streams (2 active) ===

SSRC         Codec              Pkts   Lost       Loss% Jitt    MOS  Reord Dup Duration
--------     -----              ----   ----       ----- ------  --- ----- --- --------
aabbccdd     PCMU (G.711u)      1420   3          0.2   2.1    4.48 0     0   00:01:20
11223344     Opus (dyn)          890   12          1.3   4.5    4.32 1     2   00:01:20
```

## Архитектура

| Файл | Назначение |
|------|-----------|
| `src/packet.c` | Разбор RTP-заголовка (version, CSRC, extension, padding) |
| `src/stream.c` | Хэш-таблица потоков, jitter/loss/MOS, форматирование |
| `src/capture.c` | Захват через libpcap (BPF: udp) |
| `src/capture_stub.c` | Заглушка для сборки без libpcap |
| `src/main.c` | Точка входа, цикл захвата |
| `include/voip.h` | Публичные типы и API |
| `tests/test_main.c` | 55 тестов: парсер, потоки, статистика |

## Лицензия

MIT

---

*Крафтовое ПО. Не энтерпрайз. Не фреймворк. Вкусно.*
