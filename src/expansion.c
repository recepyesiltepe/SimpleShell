#include "expansion.h"

#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

static int contains_glob_meta(const char *word) {
    return strpbrk(word, "*?[") != NULL;
}

static char *expand_tilde(const char *word, int quoted) {
    if (quoted || word[0] != '~' || (word[1] != '\0' && word[1] != '/')) {
        return xstrdup(word);
    }

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return xstrdup(word);
    }

    size_t home_len = strlen(home);
    size_t rest_len = strlen(word + 1);
    char *expanded = xmalloc(home_len + rest_len + 1);
    memcpy(expanded, home, home_len);
    memcpy(expanded + home_len, word + 1, rest_len + 1);
    return expanded;
}

int expand_word_to_command_args(Command *cmd, const char *word, int quoted) {
    char *expanded = expand_tilde(word, quoted);

    if (!quoted && contains_glob_meta(expanded)) {
        glob_t matches;
        memset(&matches, 0, sizeof(matches));
        int glob_status = glob(expanded, 0, NULL, &matches);
        if (glob_status == 0) {
            for (size_t i = 0; i < matches.gl_pathc; i++) {
                command_add_arg(cmd, matches.gl_pathv[i]);
            }
            globfree(&matches);
            free(expanded);
            return 0;
        }
        globfree(&matches);
    }

    command_add_arg(cmd, expanded);
    free(expanded);
    return 0;
}

char *expand_redirection_target(const char *word, int quoted) {
    return expand_tilde(word, quoted);
}
