# Tarea 1 (CC5308)

Este proyecto implementa una shell en C con las siguientes funcionalidades:

- Ejecucion de comandos externos usando fork + execvp.
- Builtins: cd, exit, pwd, export, unset, history.
- Redireccion de entrada y salida con `<` y `>`.

## Requisitos

- Linux
- GCC con soporte para C11
- make

## Compilacion

Desde la carpeta del proyecto:

```bash
make
```

Esto genera el ejecutable `shell`.

## Ejecucion

Para ejecutar la shell:

```bash
./shell
```

Tambien puedes compilar y ejecutar en un solo paso:

```bash
make run
```

## Limpieza

Para eliminar el ejecutable compilado:

```bash
make clean
```

## Ejemplo de redireccion

```bash
sort < input.txt > output.txt
```
