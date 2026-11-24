#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <termios.h>
#include <time.h>
#include <errno.h>

#define MAX_WORDS 5000
#define MAX_LENGTH 50
#define MAX_USED 5000
#define LOG_SIZE 512
#define LOG_MATCH 1
#define LOG_EXIT 0

char dictionary [MAX_WORDS] [MAX_LENGTH];
int dictionary_size = 0;

typedef struct {
    int type;
    char message[LOG_SIZE];
} LogMessage;

void convert_lowercase(char word[]) {
    int i;
    for (i=0; word[i] != '\0'; i++)
    {
        word[i] = (char)tolower((unsigned char) word[i]);
    }

}

int is_english_word(char word[]) {
     int i;
    if (strlen(word) < 2)
        return 0;
    for (i = 0; word[i] != '\0'; i++)
    {
        if (!isalpha((unsigned char)word[i]))
            return 0;
    }
    return 1;

}

void load_dictionary(void) {
    FILE *fp = fopen("words.txt", "r");
    if (fp == NULL)
    {
        perror("Cannot open words.txt");
        exit(EXIT_FAILURE);
    }
    char line[MAX_LENGTH];
    int duplicate;
    int i;
    while (dictionary_size < MAX_WORDS &&
           fgets(line, sizeof(line), fp) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        convert_lowercase(line);
        if (!is_english_word(line))
            continue;
        duplicate = 0;
        for (i = 0; i < dictionary_size; i++)
        {
             if (strcmp(line, dictionary[i]) == 0)
            {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
        {
            strcpy(dictionary[dictionary_size], line);
            dictionary_size++;
        }
    }
    fclose(fp);
    printf("Dictionary loaded: %d words\n", dictionary_size);

}

int check_word(char word[]) {
     int i;
    for (i = 0; i < dictionary_size; i++)
    {
        if (strcmp(word, dictionary[i]) == 0)
            return 1;
    }
    return 0;
}

int check_duplicate(char word[],  char used_words[][MAX_LENGTH],
                    int used_count) {
    int i;
    for (i = 0; i < used_count; i++)
    {
        if (strcmp(word, used_words[i]) == 0)
            return 1;
    }
    return 0;
}
 int check_chain(char word[], char previous_word[]) {
    int last = (int)strlen(previous_word) - 1;
    return word[0] == previous_word[last];
}
 int write_all(int fd, const void *buffer, size_t size) {
    const char *ptr = buffer;
    while (size > 0)
    {
        ssize_t n = write(fd, ptr, size);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        ptr += n;
        size -= (size_t)n;
    }
    return 0;
}

 int read_all(int fd, void
*buffer, size_t size) {
    char *ptr = buffer;
    while (size > 0)
    {
        ssize_t n = read(fd, ptr, size);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return 0;
        ptr += n;
        size -= (size_t)n;
    }
    return 1;
}

 int send_log(int pipe_fd, int type, const char *message) {
    LogMessage data;
    memset(&data, 0, sizeof(data));
    data.type = type;
    if (message != NULL)
    {
        strncpy(data.message, message, sizeof(data.message) - 1);
        data.message[sizeof(data.message) - 1] = '\0';
    }
    return write_all(pipe_fd, &data, sizeof(data));
}

void history_process(int read_fd) {
    FILE *fp = fopen("log.txt", "a");
    if (fp == NULL)
    {
        perror("Cannot open log.txt");
        close(read_fd);
        _exit(EXIT_FAILURE);
    }
    LogMessage data;
    while (1)
    {
        int result = read_all(read_fd, &data, sizeof(data));
        if (result == 0)
            break;
        if (result < 0)
        {
            perror("History pipe read error");
            fclose(fp);
            close(read_fd);
            _exit(EXIT_FAILURE);
        }
        if (data.type == LOG_EXIT)
            break;
        if (data.type == LOG_MATCH)
        {
            fprintf(fp, "%s\n", data.message);
            fflush(fp);
        }
    }
    fclose(fp);
    close(read_fd);
    _exit(EXIT_SUCCESS);

}

int timed_input(char word[], size_t size) {
    struct pollfd pfd;
    int ready;
    char *newline;
    int ch;

    if (!isatty(STDIN_FILENO)) {
        printf("\nPlease run the game in a terminal.\n");
        return -1;
    }
     pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    /* First 7 seconds. In canonical mode, input becomes ready after Enter. */
    do {
        ready = poll(&pfd, 1, 7000);
    } while (ready < 0 && errno == EINTR);

    if (ready == 0) {
        printf("\n!!! ONLY 3 SECONDS LEFT !!!\n");
        printf("Enter your word: ");
        fflush(stdout);

        pfd.revents = 0;
        do {
            ready = poll(&pfd, 1, 3000);
        } while (ready < 0 && errno == EINTR);
    }
     if (ready == 0) {
        /* Discard any unfinished line after the deadline. */
        tcflush(STDIN_FILENO, TCIFLUSH);
        printf("\n");
        return 0;
    }

    if (ready < 0) {
        perror("poll");
        return -1;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;

    if (!(pfd.revents & POLLIN))
        return -1;
     if (fgets(word, (int)size, stdin) == NULL)
        return -1;

    newline = strpbrk(word, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    } else {
        /* The input exceeded the buffer; consume the rest of the line. */
        do {
            ch = getchar();
        } while (ch != '\n' && ch != EOF);
        printf("Input is too long.\n");
        return -1;
    }

    convert_lowercase(word);
    return 1;    
}

void view_history(void) {
    FILE *fp = fopen("log.txt", "r");
    if (fp == NULL)
    {
        printf("\nNo game history available yet.\n");
        return;
    }
    char line[LOG_SIZE];
    printf("\n========== GAME HISTORY ==========\n");
    while (fgets(line, sizeof(line), fp) != NULL)
        printf("%s", line);
    printf("\n==================================\n");
    fclose(fp);
}

 void show_vocabulary(void) {
    int i;
    printf("\n========== VOCABULARY ==========\n");
    for (i = 0; i < dictionary_size; i++)
    {
        printf("%-20s", dictionary[i]);
        if ((i + 1) % 4 == 0)
            printf("\n");
    }
    printf("\n\nTotal words: %d\n", dictionary_size);
}

 void show_rules(void) {
    printf("\n========== HOW TO PLAY ==========\n");
    printf("1. Two players take turns.\n");
    printf("2. Use English nouns only.\n");
    printf("3. Each word starts with the previous word's last letter.\n");
    printf("4. Every word must exist in words.txt.\n");
    printf("5. Repeated words are not allowed.\n");
    printf("6. Each player has 10 seconds.\n");
    printf("7. A mistake means immediate elimination.\n");
    printf("8. The remaining player wins.\n");
    printf("=================================\n");
}

void start_game(int pipe_fd) {
    char used_words[MAX_USED][MAX_LENGTH];
    char previous_word[MAX_LENGTH] = "";
    char current_word[MAX_LENGTH];
    int used_count = 0;
    int current_player = 1;
    int winner = 0;
    time_t start_time = time(NULL);
    printf("\n========== WORD CHAIN ==========\n");
    printf(" PLAYER 1 vs PLAYER 2\n");
    printf("================================\n");
    while (used_count < MAX_USED)
    {
        printf("\n--------------------------------\n");
        printf("Player %d's turn!\n", current_player);
        if (used_count == 0)
        {
            printf("Enter any English noun: ");
        }
        else
        {
            printf("Required starting letter: %c\n",
                   previous_word[strlen(previous_word) - 1]);
            printf("Enter your word: ");
        }
        fflush(stdout);
        int input_result = timed_input(current_word,
                                       sizeof(current_word));
        int valid = 1;
        if (input_result == 0)
        {
            printf("\nTIME'S UP!\n");
            valid = 0;
        }
        else if (input_result < 0)
        {
            printf("\nInput error or word too long!\n");
            valid = 0;
        }
        else if (!is_english_word(current_word))
        {
            printf("Invalid word! Use English letters only.\n");
            valid = 0;
        }
        else if (!check_word(current_word))
        {
            printf("Word not found in dictionary!\n");
            valid = 0;
        }
        else if (check_duplicate(current_word,
                                 used_words, used_count))
        {
            printf("This word has already been used!\n");
            valid = 0;
        }
        else if (used_count > 0 &&
                 !check_chain(current_word, previous_word))
        {
            printf("Wrong starting letter!\n");
            valid = 0;
        }
        if (!valid)
        {
            winner = (current_player == 1) ? 2 : 1;
            printf("\nPlayer %d is eliminated!\n", current_player);
            printf("Player %d WINS!\n", winner);
            break;
        }
        strcpy(used_words[used_count], current_word);
        used_count++;
        strcpy(previous_word, current_word);
        printf("\nCorrect! Next player's turn.\n");
        printf("Words used: %d\n", used_count);
        current_player = (current_player == 1) ? 2 : 1;
    }
    if (used_count == MAX_USED)
    {
        winner = 0;
        printf("\nMaximum word limit reached!\n");
        printf("Match ended without a winner.\n");
    }
    time_t end_time = time(NULL);
    char date_string[30];
    struct tm *time_info = localtime(&start_time);
    if (time_info == NULL ||
        strftime(date_string, sizeof(date_string),
                 "%Y-%m-%d %H:%M:%S", time_info) == 0)
    {
        strcpy(date_string, "Unknown date");
    }
    char record[LOG_SIZE];
    sprintf(record,
            "Date: %s | Player 1 vs Player 2 | "
            "Winner: %s | Words used: %d | Duration: %ld seconds",
            date_string,
            winner == 0 ? "None" :
            winner == 1 ? "Player 1" : "Player 2",
            used_count,
            (long)(end_time - start_time));
    if (send_log(pipe_fd, LOG_MATCH, record) == 0)
        printf("\nGame result sent to history process.\n");
    else
        printf("\nCould not send game history.\n");
    printf("\n========== GAME OVER ==========\n");
}

void main_menu(int pipe_fd) {
    char choice[20];
    while (1)
    {
        printf("\n\n========== WORD CHAIN ==========\n");
        printf("1. Start New Game\n");
        printf("2. View Game History\n");
        printf("3. View Vocabulary\n");
        printf("4. How to Play\n");
        printf("5. Exit\n");
        printf("================================\n");
        printf("Select an option: ");
        if (fgets(choice, sizeof(choice), stdin) == NULL)
            break;
        switch (atoi(choice))
        {
            case 1:
                start_game(pipe_fd);
                break;
            case 2:
                view_history();
                break;
            case 3:
                show_vocabulary();
                break;
            case 4:
                show_rules();
                break;
            case 5:
                printf("\nThank you for playing WORD CHAIN!\n");
                return;
            default:
                printf("\nInvalid menu selection!\n");
        }
    }
}

int main(void) {
    int fd[2];
    printf("================================\n");
    printf(" WORD CHAIN GAME\n");
    printf("================================\n");
    load_dictionary();
    if (dictionary_size == 0)
    {
        printf("Dictionary is empty!\n");
        return EXIT_FAILURE;
    }
    /* Create pipe */
    if (pipe(fd) == -1)
    {
        perror("pipe");
        return EXIT_FAILURE;
    }
    /* Create child process */
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return EXIT_FAILURE;
    }
    if (pid == 0)
    {
        /* Child process */
        close(fd[1]);
        history_process(fd[0]);
    }
    else
    {
        /* Parent process */
        close(fd[0]);
        main_menu(fd[1]);
        /* Tell child process to finish */
        if (send_log(fd[1], LOG_EXIT, NULL) == -1)
            perror("Sending exit message");
        close(fd[1]);
        /* Wait for child process */
        int status;
        while (waitpid(pid, &status, 0) == -1)
        {
            if (errno != EINTR)
            {
                perror("waitpid");
                break;
            }
        }
        printf("History process completed.\n");
    }
    return EXIT_SUCCESS;
}