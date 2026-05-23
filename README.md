Códigos para probar el funcionamiento del micrófono INMP441 (comunicaciones.ino), del amplificador MAX98357A (prueba_amp.ino), de la placa LILYGO T-A7670G (PruebaLTE.ino), de conexión a servidor relay (prueba_servidor.ino) y de grabación y reproducción (prueba_grabacion_y_reproduccion.ino).

Las pruebas se deben ejecutar una por una en Arduino IDE, con el gestor de placas de ESP32 previamente instalado y la opción de placa "ESP32 Dev Module" escogida. Además, para la prueba del amplificador, se deben instalar las librerías Arduino Audio Tools (disponible en https://github.com/pschatzmann/arduino-audio-tools) y ESP32 A2DP (disponible en https://github.com/pschatzmann/ESP32-A2DP). También se encuentran en una carpeta comprimida en el repositorio de código. La prueba de conexión a servidor relay se debe realizar con el servidor encendido. Cada prueba se realiza con una conexión/montaje en específico, que se indica tanto dentro del mismo código como en los respectivos esquemáticos.

Videos mostrando resultados de pruebas de funcionamiento:

https://youtu.be/dOvd_48lKlA (micrófono y amplificador)
https://youtu.be/FoEUtrB1_nY (placa LILYGO)
