# Code zur Bachelorarbeit zur echtzeitgewahren Binärübersetzung
Ziel der Bachelorarbeit war der Entwurf eines echtzeitgewahren statischen Binärübersetzers. Dieser soll Binärcode für den 6502-Prozessor in Binärcode für den ATmega 328P übersetzen.

### Threaded 6502 Emulator
Der emulator Ordner enthält einen Threaded Interpreter. Dieser wurde als Basis für den Binärübersetzer, insbesondere für die Erstellung der C Code Repräsentation, verwendet.
Die C Code Repräsentation wurde mit den Test von Ruud Baltissen vollständig und mit den Tests von Klaus Dormann teilweise getestet.
Die Tests von Ruud Baltissen sind im Ordner Ruud_Baltissen_Tests zu finden. Die Tests von Klaus Dormann sind im Ordner Klaus_Dormann_Tests.
Zum Kompilieren des Emulators sollte das beiliegende Makefile verwendet werden. Durch Entfernen der -DDECIMALMODE Compiler Flag im Makefile kann der EMulator auch ohne Binary Codes Decimal (BCD) Modus kompiliert werden.

### echtzeitgewahrer statischer Binärübersetzer
Der entwickelte echtzeitgewahre statische Binärübersetzer ist im Ordner echtzeit_gewahrer_statischer_binaeruebersetzer zu finden.
In der Bachelorarbeit wurden verschiedene Möglichkeiten der echtzeitgewahren stattischen Binärübersetzung thematisiert. Da diese auch mit Hilfe des entworfenen echtzeitgewahren statischen Binärübersetzers evaluiert werden sollten, wird für den echtzeitgewahren statischen Binärübersetzer bedingte Kompilierung, sowie ein modularer Ansatz, gewählt.

### Module
translator.c
------------
Diese Modul enthält analysiert und disassembliert das Binärprogramm und steuert die Codegenerierung. Es enthält auch die Optimierung der Flags und C-Hilfskonstrukte für die AVR-Inline-Assembler-Repräsentation.

6502_instructions_c.c
---------------------
Dieses Modul enthält die C-Code Repräsentation. Es wird sowohl für Opcodes und Adressierungsarten spezifische Analysen, als auch für die Generierung der C-Code Repräsentation verwendet. Die enthaltenen "Illegalen" Opcode des 6502 sind jedoch nicht für die Übersetzung verwendbar. Auch der enthaltene BCD-Modus sollte nicht verwendet werden, da er im Rahmen der Bachelorarbeit nicht getestet wurde.

6502_instructions_avr.c
-----------------------
Dieses Modul enthält die AVR-Inline-Assembler Repräsentation. Genauso wie das 6502_instructions_c.c Modul, wird es für Opcodes und Adressierungsarten spezifische Analysen und die Generierung verwendet. Die enthaltenen "Illegalen" Opcode des 6502 sind ebenfalls nicht für die Übersetzung verwendbar. Der BCD-Modus ist nicht vollständig implementiert und würde bei der Übersetzung daher weitestgehend ignoriert werden.

6502_instructions.h
-------------------
Diese Headerdatei ist die Schnittstelle zwischen den Repräsentations spezifischen Modulen und dem translator.c Modul. 

Compiler Flags
--------------
Die -DC Flag sorgt dafür, dass vom Binärübersetzer der Dispatch-Code für Rücksprünge aus Subroutinen für die C-Code Repräsentationen generiert wird. 
Die -DAVR Flag sorgt für das Einfügen von Labels mittels Inline-Assembler. Diese in den Assembler-Code eingefügten Labels ermöglichen einen Sprung in eine Subroutine für die AVR-Inline-Assembler Repräsentation.
-DC sollte nur zusammen mit dem C-Code spezifische und -DAVR nur zusammen mit dem AVR-Inline-Assembler spezifischen Modul verwendet werden. 

Die -DOPTIMIZATION Flag ermöglicht zu spezifizieren, ob die in der Bachelorarbeit vorgestellte Optimierung verwendet werden soll. Ist sie gesetzt, wird die Optimierung verwendet.
Die AVR-Inline-Assembler Repräsentation sollte, wie in der Bachelorarbeit beschrieben, nicht ohne Optimierung verwendet werden.

Die -DWCET Flag sorgt für die Verwendung der WCET. Diese wird dann zur Synchronisation der ATmega 328P Prozessorzyklen mit den 6502-Prozessorzyklen verwendet. Ist die Flag nicht gesetzt wird die BCET dafür verwendet.

### Beispielprogramme
Das erste Beispielprogramm ist in Form des test_program Arrays im translator.c Modul zu finden.
Das zweite Beispielprogramm ist in abc_300.bin zu finden. 
Beide Programme geben alle Großbuchstaben des Alphabets aus. Das zweite Programm wartet zwischen der Ausgabe der Großbuchstaben ca. 300 Millisekunden.
Die für das erste Programm generierten Programme sind im Ordner generierter_Code_Alphabetprogramm zu finden. Es gibt dabei jeweils eine mittels der WCET und eine mittels der BCET synchronisierte Version.
Dies gilt auch für das zweite Beispielprogramm. Die generierten Programme für dieses Beispiel sind im Ordner generierter_Code_Alphabetprogramm_mit_Verzögerung zu finden.
Alle generierten Programme können mit dem beiliegenden Makefile auf einen Arduino Nano geladen werden. Dazu muss das Makefile, wie im Makefile selbst beschrieben, angepasst werden.

### Entwicklungs- und Ausführungsumgebung
Die entwickelten Programme wurden auf einem Linux Ubuntu 22.04 Betriebssystem entwickelt.
Der echtzeitgewahre statische Binärübersetzer, sowie auch der Emulator, wurden mit GCC 11.4.0 kompiliert.
Die generierten Programme wurden auf einem Arduino Nano mit ATmega 328P Prozessor ausgeführt. Zur Kompilierung wurde der AVR-GCC Compiler 5.4.0 verwendet. 
