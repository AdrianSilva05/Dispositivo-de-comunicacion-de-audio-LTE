# Sección de pruebas iniciales

Los siguientes códigos fueron utilizados para **pruebas iniciales, aún sin transmisión LTE**. Funcionan para probar el funcionamiento del:

+ **Micrófono INMP441** (comunicaciones.ino).
+ **Amplificador MAX98357A** (prueba_amp.ino). 
+ **Conexión a red LTE** (PruebaLTE.ino).
+ **Conexión a servidor relay** (prueba_servidor.ino).
+ **Grabación y reproducción** (prueba_grabacion_y_reproduccion.ino).

Las pruebas se deben ejecutar una por una en **Arduino IDE**, con el **gestor de placas de ESP32** previamente instalado y la **opción de placa "ESP32 Dev Module"** escogida. Además, para la prueba del amplificador, se deben instalar las librerías:

+ **Arduino Audio Tools** (disponible en https://github.com/pschatzmann/arduino-audio-tools).
+ **ESP32 A2DP** (disponible en https://github.com/pschatzmann/ESP32-A2DP).

También se encuentran en una carpeta comprimida en el repositorio de código. La prueba de conexión a servidor relay se debe realizar con el servidor encendido. Cada prueba se realiza con una conexión/montaje en específico, que se indica tanto dentro del mismo código como en los respectivos esquemáticos.

Videos mostrando resultados de pruebas de funcionamiento:

+ https://youtu.be/dOvd_48lKlA (**micrófono y amplificador**).
+ https://youtu.be/FoEUtrB1_nY (**placa LILYGO**).

# Sección de pruebas finales

Para **transmisión y recepción mediante LTE en streaming** (habla y escucha continua, sin grabación ni delay alto), los siguientes códigos entregaron los mejores resultados:

+ **Módulo Tx** (streaming_Tx.ino)
+ **Servidor** (streaming_Servidor.txt) 
+ **Módulo Rx** (streaming_Rx.ino)

Antes de realizar la prueba, se debe asegurar que:

+ El servidor esté encendido.
+ Cada tarjeta SIM esté insertada en un módulo (preferiblemente ambas del mismo operador (en este caso, Claro))
+ El hardware funcione correctamente, mediante pruebas iniciales como la de **Grabación y reproducción** o la de **Conexión a red LTE**. 
