# MaGalerie-3DS

Une petite galerie photo pour Nintendo 3DS modifiee. Elle lit les images depuis la carte SD, donc **aucune photo personnelle n'a besoin d'etre publiee sur GitHub**.

## Fonctions

- affichage sur l'ecran du haut ;
- navigation gauche/droite ;
- zoom haut/bas ;
- rechargement de la liste avec X ;
- photos triees par nom de fichier.

## Format des photos

Pour rester simple et fiable, cette version accepte les fichiers :

- `.bmp` ;
- BMP 24 bits ou 32 bits ;
- sans compression.

Tu peux convertir une photo JPG/PNG en BMP avec une application ou un convertisseur d'images sur iPhone.

## Emplacement sur la carte SD

Place les photos ici avec FTPD :

```text
/3ds/MaGalerie/photos/
```

Exemple :

```text
/3ds/MaGalerie/photos/01-vacances.bmp
/3ds/MaGalerie/photos/02-famille.bmp
```

Place ensuite l'application compilee ici :

```text
/3ds/MaGalerie-3DS/MaGalerie-3DS.3dsx
```

Puis lance-la depuis **Homebrew Launcher**.

## Commandes

| Bouton | Action |
|---|---|
| Gauche / Droite | Photo precedente / suivante |
| Haut / Bas | Zoom avant / arriere |
| X | Recharger les photos |
| START | Quitter |

## Compiler avec GitHub

Une compilation automatique est fournie dans `.github/workflows/build.yml`.

1. Envoie tous les fichiers du projet dans ton depot GitHub.
2. Ouvre l'onglet **Actions** du depot.
3. Ouvre l'action **Compiler MaGalerie-3DS**.
4. Attends qu'elle soit terminee.
5. Telecharge l'artefact **MaGalerie-3DS**.
6. Decompresse-le pour obtenir `MaGalerie-3DS.3dsx`.

## Compiler sur un ordinateur

Avec devkitPro et le groupe de paquets 3DS installes :

```bash
make
```

## Confidentialite

Le dossier `photos/` de ce depot ne contient aucune vraie photo. Les images doivent rester uniquement sur ta carte SD.
