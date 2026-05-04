# Gestionnaire de Système de Fichiers

## Présentation du Projet

Ce projet est une simulation de système de fichiers développée dans le cadre de l’UE Systèmes d’Exploitation à l’Université de Cergy-Pontoise.
Il reproduit les principales fonctionnalités d’un système de fichiers UNIX : gestion des fichiers et répertoires, permissions, liens symboliques et physiques, sauvegardes, et optimisation de l’espace disque.

## 👨‍💻 Auteurs

- Inas Boukili  
- Fella Meziane  
- Dara Offiong  

**Groupe** : C3  
**Date** : 1er avril 2025

## Fonctionnalités

1. **Opérations sur Fichiers et Répertoires** : Création / suppression (`touch`, `mkdir`, `rm`)
2. **Copie et déplacement** : (`cp`, `mv`)
3. **Navigation hiérarchique entre répertoires**
4. **Gestion des Permissions** : Simulation des commandes `chmod` et `chown`
5. **Gestion des Liens** : Liens physiques (inodes partagés)
6. **Liens symboliques** : (chemins de référence)
7. **Gestion du Disque** : Allocation efficace via bitmap
8. **Suppression des fichiers et libération mémoire**
9. **Analyse et défragmentation de l’espace disque**
10. **Système de Sauvegarde et restauration des versions précédentes**


## Guide d’Installation et d’Utilisation

### Installation

1. **Prérequis** :
   - Système UNIX/Linux  
   - Compilateur `gcc` installé  
   - `make` installé

2. **Téléchargement et compilation** :
   ```bash
   tar -xvzf MEZIANE-BOUKILI-OFFIONG.tgz
   make run
   ``` 
### Utilisation

- Pour lancer le programme, utilisez la commande suivante :
  ```bash
  cd /mnt/c/Users/lYourUsername/Downloads/MEZIANE-BOUKILI-OFFIONG/MEZIANE-BOUKILI-OFFIONG/src
  make run
  ```

- Des tests automatisés seront exécutés automatiquement. Une fois ceux-ci terminés, l'utilisateur peut saisir des commandes directement dans la console.

- Tapez `help` pour afficher la liste des commandes disponibles ainsi que les arguments possibles.

## Conclusion

Ce mini-projet nous a permis de comprendre en profondeur la gestion bas niveau d’un système de fichiers. L’implémentation de commandes UNIX nous a donné une meilleure vision des mécanismes internes que l’on utilise quotidiennement.