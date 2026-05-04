/*
 * Fichier : gestionnaire.c
 * Auteurs :
 *  - MEZIANE Fella - 33.33%
 *  - BOUKILI Inas - 33.33%
  * - OFFIONG Dara - 33.33%
 * Date de création : 15/02/2025
 * Dernière modification : 05/04/2025
 */
#include "gestionnaire.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void print_prompt() {
    printf("\033[1;32m%s\033[0m$ ", fs.current_path);
}

void simulate_typing(const char* command) {
    // Simule un délai de frappe
    usleep(100000);
    printf("%s\n", command);
}

void run_automated_tests() {
    printf("\n=== LANCEMENT DES TESTS INTERACTIFS ===\n\n");

    // Initialisation
    simulate_typing("init_fs");
    init_fs();
    printf("Système de fichiers initialisé\n\n");

    // Commandes de base
    print_prompt();
    simulate_typing("mkdir test_dir");
    fs_mkdir("test_dir", 0755, 0);
    
    print_prompt();
    simulate_typing("touch fichier1.txt");
    fs_touch("fichier1.txt");

    // Test ls
    print_prompt();
    simulate_typing("ls");
    fs_ls(NULL);
    printf("\n");

    // Test cd
    print_prompt();
    simulate_typing("cd test_dir");
    fs_chdir("test_dir");
    
    print_prompt();
    simulate_typing("pwd");
    char cwd[MAX_PATH];
    fs_getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);

    // Test création fichier
    print_prompt();
    simulate_typing("touch sous_fichier.txt");
    fs_touch("sous_fichier.txt");
    printf("\n");

    // Test ls -l
    print_prompt();
    simulate_typing("ls -l");
    fs_ls_l(NULL);
    printf("\n");

    // Retour racine
    print_prompt();
    simulate_typing("cd /");
    fs_chdir("/");
    printf("\n");

    // Test mkdir -p
    print_prompt();
    simulate_typing("mkdir -p dir1/dir2/dir3");
    fs_mkdir("dir1/dir2/dir3", 0755, 1);
    printf("\n");

    // Test tree
    print_prompt();
    simulate_typing("tree");
    fs_tree(NULL);
    printf("\n");

    // Test cp
    print_prompt();
    simulate_typing("cp fichier1.txt fichier_copie.txt");
    fs_copy("fichier1.txt", "fichier_copie.txt");
    printf("\n");

    // Test mv
    print_prompt();
    simulate_typing("mv fichier_copie.txt fichier_deplace.txt");
    fs_move("fichier_copie.txt", "fichier_deplace.txt", fs.current_dir);
    printf("\n");

    // Test echo
    print_prompt();
    simulate_typing("echo \"Contenu test\" > fichier_echo.txt");
    fs_echo("Contenu test", "fichier_echo.txt");
    printf("\n");

    // Test cat
    print_prompt();
    simulate_typing("cat fichier_echo.txt");
    fs_cat("fichier_echo.txt");
    printf("\n");

    // Test chmod
    print_prompt();
    simulate_typing("chmod 644 fichier1.txt");
    fs_chmod("fichier1.txt", 0644);
    printf("\n");

    // Test liens
    print_prompt();
    simulate_typing("ln fichier1.txt lien_physique");
    fs_link("fichier1.txt", "lien_physique");
    
    print_prompt();
    simulate_typing("ln -s fichier1.txt lien_symbolique");
    fs_symlink("fichier1.txt", "lien_symbolique");
    printf("\n");

    // Test tree avec inodes
    print_prompt();
    simulate_typing("tree --inodes");
    fs_tree_inodes(NULL);
    printf("\n");

    // Nettoyage
    print_prompt();
    simulate_typing("rm fichier_deplace.txt");
    fs_unlink("fichier_deplace.txt");
    
    print_prompt();
    simulate_typing("rmdir dir1/dir2/dir3");
    fs_rmdir("dir1/dir2/dir3");
    printf("\n");
    printf("\n\033[1;32m=== TESTS AUTOMATISÉS TERMINÉS ===\033[0m\n");
    printf("\nVous pouvez maintenant tester manuellement d'autres commandes.\n");
    printf("Tapez 'help' pour voir la liste des commandes disponibles.\n");
    printf("Tapez 'exit' pour quitter.\n\n");
}

int main() {
    run_automated_tests();
    // Passage en mode interactif
    shell();
    return 0;
}