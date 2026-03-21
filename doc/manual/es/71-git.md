
## Programación en C

[**pgmoneta**](https://github.com/pgmoneta/pgmoneta) se desarrolla utilizando el [lenguaje de programación C](https://en.wikipedia.org/wiki/C_(programming_language)) por lo que es una buena idea tener algunos conocimientos sobre el lenguaje antes de comenzar a hacer cambios.

Hay libros como:

* [C in a Nutshell](https://www.oreilly.com/library/view/c-in-a/9781491924174/)
* [21st Century C](https://www.oreilly.com/library/view/21st-century-c/9781491904428/)

que pueden ayudarte

### Debugging

Para depurar problemas en tu código puedes usar [gdb](https://www.sourceware.org/gdb/), o agregar registro adicional usando la API `pgmoneta_log_XYZ()`

## Guía de Git

Aquí hay algunos enlaces que te ayudarán

* [How to Squash Commits in Git][git_squash]
* [ProGit book][progit]

### Pasos básicos

**Empieza haciendo Frok al repositorio**

Esto se haces mediante el botón "Fork" en GitHub.

**Clona tu repositorio localmente**

Esto se hace con

```sh
git clone git@github.com:<username>/pgmoneta.git
```

**Agrega upstream**

Ejecuta

```sh
cd pgmoneta
git remote add upstream https://github.com/pgmoneta/pgmoneta.git
```

**Crea una rama de trabajo**

```sh
git checkout -b mywork main
```

**Realiza los cambios**

Recuerda verificar la compilación y ejecución del código.

Usa

```
[#xyz] Descripción
```

como mensaje de commit donde `[#xyz]` es el número del problema del trabajo, y `Descripción` es una descripción breve del problema en la primera línea

**Múltiples commits**

Si tienes múltiples commits en tu rama, entonces hazles squash

``` sh
git rebase -i HEAD~2
```

por ejemplo. Es `p` para el primero, luego `s` para el resto

**Rebase**

Siempre haz rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

**Force push**

Cuando hayas terminado con tus cambios, haz push forzado de tu rama

``` sh
git push -f origin mywork
```

y luego crea una pull request

**Formatea el código fuente**

Usa

``` sh
./clang-format.sh
```

para formatear el código fuente. Se requiere clang-format 21+.

**Repetir**

Basado en la retroalimentación que recibas, continúa realizando cambios, haciendo squashing, rebasing y haciendo push force

**PTAL**

Cuando estés trabajando en un cambio, colócalo en modo Borrador, para que sepamos que aún no estás satisfecho con él.

Por favor, envía un PTAL al Committer que te fue asignado una vez que creas que tu cambio está completo. Y, por supuesto, extrae el modo Borrador.

**Deshacer**

Normalmente puedes restablecer a un commit anterior usando `git reset <commit hash> --hard`.

Pero si accidentalmente aplastaste dos o más commits, y deseas deshacerlo, necesitas saber dónde restablecer, y el commit parece haber desaparecido después de que rebases.

Pero en realidad no se pierden - usando `git reflog`, puedes encontrar cada commit al que el HEAD ha apuntado. Encuentra el commit al que deseas restablecer, y ejecuta `git reset --hard`.
