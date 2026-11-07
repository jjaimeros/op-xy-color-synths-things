# Parches para Open-XY v0.0.5

- `opxy-propio-0.0.5.patch`: los carretes de la pantalla de cinta giran con play o con una tecla de cinta pulsada, con velocidad según los potes 1 y 2 (`tape_reels.cpp`), y las pantallas de los motores reciben `draw::Input.playing`. Twin Osc lo necesita para compilar.
- `tempo-en-pantalla.patch`: las pantallas reciben también el tempo y la posición de la canción (`bpm_x10` y `ticks96`). Raver lo necesita para compilar. Además, la web de pruebas muestra los porcentajes sin decimales.

```sh
git clone --branch v0.0.5 https://github.com/etcetc000/open-xy.git
cd open-xy
git apply ../op-xy-color-synths-things/sdk-patch/opxy-propio-0.0.5.patch
git apply ../op-xy-color-synths-things/sdk-patch/tempo-en-pantalla.patch
```

Para la cinta animada, la pantalla debe tener los nodos `reel_l0..3` y `reel_r0..3`. Sin ellos se dibuja igual, pero quieta.

Solo funciona con el firmware 1.2.1 del OP-XY, porque modifica una llamada concreta de la pantalla de cinta. Licencia MIT, como Open-XY.
