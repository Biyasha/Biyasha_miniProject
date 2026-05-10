/*
 * ============================================================
 *  BANKING MANAGEMENT SYSTEM - banking.h
 *  Header file: constants, structs, and function prototypes
 * ============================================================
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* ─── File names ─────────────────────────────────────────── */
#define DB_FILE          "credit.dat"
#define ACCOUNTS_TXT     "accounts.txt"
#define TRANS_FILE       "transactions.log"

/* ─── System limits ──────────────────────────────────────── */
#define MAX_ACCOUNTS     100
#define MAX_LAST_NAME    15
#define MAX_FIRST_NAME   10
#define MIN_BALANCE      500.00   /* Minimum balance policy  */
#define ATM_DAILY_LIMIT  20000.00 /* Daily ATM withdrawal cap */
#define MAX_PIN_LEN      5        /* 4-digit PIN + null       */
#define MAX_PASS_LEN     32


/* ─── Transaction types ──────────────────────────────────── */
#define TXN_DEPOSIT      'D'
#define TXN_WITHDRAW     'W'
#define TXN_TRANSFER     'T'
#define TXN_CREATE       'C'
#define TXN_DELETE       'X'

/* ─── Return / status codes ──────────────────────────────── */
#define SUCCESS           0
#define ERR_FILE         -1
#define ERR_NOT_FOUND    -2
#define ERR_EXISTS       -3
#define ERR_MIN_BALANCE  -4
#define ERR_LIMIT        -5
#define ERR_INVALID      -6
#define ERR_AUTH         -7

/* ═══════════════════════════════════════════════════════════
 *  Core data structures
 * ═══════════════════════════════════════════════════════════ */

/* Account record – fixed-size for random-access storage */
typedef struct {
    unsigned int acct_num;             /* 1-based slot number   */
    char         last_name[MAX_LAST_NAME];
    char         first_name[MAX_FIRST_NAME];
    double       balance;
    char         pin[MAX_PIN_LEN];     /* 4-digit PIN (hashed)  */
    int          is_active;            /* 1 = active, 0 = deleted */
    double       daily_withdrawn;      /* resets each calendar day */
    char         last_txn_date[11];    /* YYYY-MM-DD             */
} ClientData;

/* Transaction log entry */
typedef struct {
    unsigned int acct_num;
    char         type;                 /* D/W/T/C/X              */
    double       amount;
    double       balance_after;
    char         timestamp[20];        /* YYYY-MM-DD HH:MM:SS    */
    char         description[40];
} Transaction;


/* ═══════════════════════════════════════════════════════════
 *  Function prototypes
 * ═══════════════════════════════════════════════════════════ */

/* ── Initialisation ──────────────────────────────────────── */
void  init_database(void);
int   verify_account_pin(FILE *fp, unsigned int acct_num);

/* ── UI / menu helpers ───────────────────────────────────── */
void  clear_screen(void);
void  print_banner(void);
void  print_separator(char ch, int width);
void  print_menu(void);
void  print_account_row(const ClientData *c);
void  print_account_header(void);
void  pause_prompt(void);
unsigned int get_menu_choice(int min, int max);

/* ── Input helpers ───────────────────────────────────────── */
void  flush_input(void);
int   get_valid_account_num(const char *prompt);
double get_positive_double(const char *prompt);
void  get_string(const char *prompt, char *buf, int max_len);
void  get_pin(const char *prompt, char *pin_buf);

/* ── Core CRUD operations ────────────────────────────────── */
int   create_account(FILE *fp);
int   display_all_accounts(FILE *fp);
int   search_account(FILE *fp);
int   update_account_balance(FILE *fp);
int   delete_account(FILE *fp);

/* ── Banking transactions ────────────────────────────────── */
int   deposit(FILE *fp);
int   atm_withdraw(FILE *fp);
int   transfer_funds(FILE *fp);

/* ── Reports & utilities ─────────────────────────────────── */
int   export_text_report(FILE *fp);
int   view_transaction_history(unsigned int acct_num);
void  sort_accounts_by_balance(FILE *fp);
void  sort_accounts_by_name(FILE *fp);

/* ── File I/O helpers ────────────────────────────────────── */
int   read_record(FILE *fp, unsigned int slot, ClientData *out);
int   write_record(FILE *fp, unsigned int slot, const ClientData *in);
void  log_transaction(unsigned int acct_num, char type,
                      double amount, double bal_after,
                      const char *desc);
void  get_timestamp(char *buf, size_t len);
void  reset_daily_limit_if_needed(ClientData *c);


/*
 * ============================================================
 *  BANKING MANAGEMENT SYSTEM - utils.c
 *  UI helpers, input validation, file I/O primitives,
 *  authentication, and transaction logging.
 * ============================================================
 */


/* ─── simple string-hash for PIN storage ─────────────────── */
static unsigned int simple_hash(const char *s)
{
    unsigned int h = 5381u;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h;
}

/* ══════════════════════════════════════════════════════════
 *  Database / auth initialisation
 * ══════════════════════════════════════════════════════════ */

/*
 * init_database – create credit.dat if it does not exist.
 * Writes MAX_ACCOUNTS blank ClientData records so every slot
 * can be addressed by fseek without gaps.
 */
void init_database(void)
{
    FILE *fp = fopen(DB_FILE, "rb");
    if (fp) { fclose(fp); return; }          /* already exists  */

    fp = fopen(DB_FILE, "wb");
    if (!fp) { perror("init_database"); exit(EXIT_FAILURE); }

    ClientData blank = {0, "", "", 0.0, "", 0, 0.0, ""};
    for (int i = 0; i < MAX_ACCOUNTS; i++)
        fwrite(&blank, sizeof(ClientData), 1, fp);

    fclose(fp);
    printf("  [INFO] New database '%s' created.\n", DB_FILE);
}

/* ══════════════════════════════════════════════════════════
 *  Authentication
 * ══════════════════════════════════════════════════════════ */

/*
 * verify_account_pin – reads the stored PIN for acct_num and
 * compares against user input.  Returns 1 if correct.
 */
int verify_account_pin(FILE *fp, unsigned int acct_num)
{
    ClientData c;
    if (read_record(fp, acct_num, &c) != SUCCESS || !c.is_active)
        return 0;

    /* If no PIN is set, access is open (legacy accounts) */
    if (c.pin[0] == '\0') return 1;

    char entered[MAX_PIN_LEN];
    get_pin("  Enter account PIN: ", entered);

    char hashed[16];
    snprintf(hashed, sizeof(hashed), "%u", simple_hash(entered));
    return (strcmp(hashed, c.pin) == 0) ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════
 *  UI helpers
 * ══════════════════════════════════════════════════════════ */

void clear_screen(void)
{
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void print_separator(char ch, int width)
{
    for (int i = 0; i < width; i++) putchar(ch);
    putchar('\n');
}

void print_banner(void)
{
    clear_screen();
    print_separator('*', 58);
    printf("*%*s*\n", 56, "");
    printf("*   %-52s   *\n", "");
    printf("*       PROFESSIONAL BANKING MANAGEMENT SYSTEM       *\n");
    printf("*              CS3251 Mini Project – Lab              *\n");
    printf("*%*s*\n", 56, "");
    print_separator('*', 58);
    putchar('\n');
}

void print_menu(void)
{
    print_separator('-', 58);
    printf("  MAIN MENU\n");
    print_separator('-', 58);
    printf("   ACCOUNT MANAGEMENT\n");
    printf("   1. Create New Account\n");
    printf("   2. Display All Accounts\n");
    printf("   3. Search Account\n");
    printf("   4. Update Balance (Manual)\n");
    printf("   5. Delete Account\n\n");

    printf("   BANKING TRANSACTIONS\n");
    printf("   6. Deposit Funds\n");
    printf("   7. ATM Withdrawal\n");
    printf("   8. Transfer Funds\n\n");

    printf("   REPORTS & UTILITIES\n");
    printf("   9.  Export accounts.txt Report\n");
    printf("   10. View Transaction History\n");
    printf("   11. Sort Accounts by Balance\n");
    printf("   12. Sort Accounts by Name\n\n");

    printf("   0.  Exit\n");
    print_separator('-', 58);
}

void print_account_header(void)
{
    printf("\n");
    print_separator('-', 68);
    printf("  %-6s %-15s %-10s %12s  %s\n",
           "Acct", "Last Name", "First Name", "Balance", "Status");
    print_separator('-', 68);
}

void print_account_row(const ClientData *c)
{
    printf("  %-6u %-15s %-10s %12.2f  %s\n",
           c->acct_num, c->last_name, c->first_name,
           c->balance, c->is_active ? "ACTIVE" : "CLOSED");
}

void pause_prompt(void)
{
    printf("\n  Press ENTER to continue...");
    flush_input();
    getchar();
}

unsigned int get_menu_choice(int min, int max)
{
    int choice;
    char buf[16];
    while (1) {
        printf("  Choice [%d-%d]: ", min, max);
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%d", &choice) == 1 &&
                choice >= min && choice <= max)
                return (unsigned int)choice;
        }
        printf("  [!] Invalid option. Try again.\n");
    }
}

/* ══════════════════════════════════════════════════════════
 *  Input helpers
 * ══════════════════════════════════════════════════════════ */

/* Discard characters up to and including the next newline */
void flush_input(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/*
 * get_valid_account_num – prompts until the user enters an
 * integer in [1, MAX_ACCOUNTS].
 */
int get_valid_account_num(const char *prompt)
{
    int num;
    char buf[16];
    while (1) {
        printf("  %s", prompt);
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%d", &num) == 1 &&
                num >= 1 && num <= MAX_ACCOUNTS)
                return num;
        }
        printf("  [!] Account number must be between 1 and %d.\n",
               MAX_ACCOUNTS);
    }
}

/*
 * get_positive_double – prompts until the user enters a
 * positive floating-point value.
 */
double get_positive_double(const char *prompt)
{
    double val;
    char buf[32];
    while (1) {
        printf("  %s", prompt);
        if (fgets(buf, sizeof(buf), stdin)) {
            if (sscanf(buf, "%lf", &val) == 1 && val > 0.0)
                return val;
        }
        printf("  [!] Please enter a positive amount.\n");
    }
}

/*
 * get_string – read a non-empty string up to max_len-1 chars.
 */
void get_string(const char *prompt, char *buf, int max_len)
{
    char tmp[256];
    while (1) {
        printf("%s", prompt);
        if (fgets(tmp, sizeof(tmp), stdin)) {
            tmp[strcspn(tmp, "\n")] = '\0';   /* strip newline */
            if (strlen(tmp) > 0 && (int)strlen(tmp) < max_len) {
                strncpy(buf, tmp, max_len - 1);
                buf[max_len - 1] = '\0';
                return;
            }
        }
        printf("  [!] Input must be 1-%d characters.\n", max_len - 1);
    }
}

/*
 * get_pin – reads up to 4 digits; on real terminals this would
 * use termios to hide input.  Portable fallback shown here.
 */
void get_pin(const char *prompt, char *pin_buf)
{
    char tmp[32];
    printf("%s", prompt);
    if (fgets(tmp, sizeof(tmp), stdin)) {
        tmp[strcspn(tmp, "\n")] = '\0';
        strncpy(pin_buf, tmp, MAX_PIN_LEN - 1);
        pin_buf[MAX_PIN_LEN - 1] = '\0';
    }
}

/* ══════════════════════════════════════════════════════════
 *  Low-level file I/O
 * ══════════════════════════════════════════════════════════ */

/*
 * read_record – reads the ClientData at slot (1-based) into
 * *out.  Returns SUCCESS or ERR_FILE.
 */
int read_record(FILE *fp, unsigned int slot, ClientData *out)
{
    if (slot < 1 || slot > MAX_ACCOUNTS) return ERR_INVALID;
    long offset = (long)(slot - 1) * (long)sizeof(ClientData);
    if (fseek(fp, offset, SEEK_SET) != 0) return ERR_FILE;
    if (fread(out, sizeof(ClientData), 1, fp) != 1) return ERR_FILE;
    return SUCCESS;
}

/*
 * write_record – writes *in to slot (1-based).
 * Returns SUCCESS or ERR_FILE.
 */
int write_record(FILE *fp, unsigned int slot, const ClientData *in)
{
    if (slot < 1 || slot > MAX_ACCOUNTS) return ERR_INVALID;
    long offset = (long)(slot - 1) * (long)sizeof(ClientData);
    if (fseek(fp, offset, SEEK_SET) != 0) return ERR_FILE;
    if (fwrite(in, sizeof(ClientData), 1, fp) != 1) return ERR_FILE;
    fflush(fp);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  Transaction logging
 * ══════════════════════════════════════════════════════════ */

/*
 * get_timestamp – fills buf with the current date/time in the
 * format "YYYY-MM-DD HH:MM:SS".
 */
void get_timestamp(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

/*
 * log_transaction – appends a single Transaction record to
 * TRANS_FILE (binary log).  Silently ignores write errors so
 * that a logging failure never crashes the main program.
 */
void log_transaction(unsigned int acct_num, char type,
                     double amount, double bal_after,
                     const char *desc)
{
    FILE *fp = fopen(TRANS_FILE, "ab");
    if (!fp) return;

    Transaction t;
    memset(&t, 0, sizeof(t));
    t.acct_num     = acct_num;
    t.type         = type;
    t.amount       = amount;
    t.balance_after = bal_after;
    get_timestamp(t.timestamp, sizeof(t.timestamp));
    strncpy(t.description, desc, sizeof(t.description) - 1);

    fwrite(&t, sizeof(Transaction), 1, fp);
    fclose(fp);
}

/*
 * reset_daily_limit_if_needed – if the last transaction date
 * differs from today, resets the daily withdrawal counter.
 */
void reset_daily_limit_if_needed(ClientData *c)
{
    char today[11];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(today, sizeof(today), "%Y-%m-%d", t);

    if (strcmp(c->last_txn_date, today) != 0) {
        c->daily_withdrawn = 0.0;
        strncpy(c->last_txn_date, today, sizeof(c->last_txn_date) - 1);
    }
}
/*
 * ============================================================
 *  BANKING MANAGEMENT SYSTEM - operations.c
 *  All CRUD, banking transaction, and report functions.
 * ============================================================
 */


/* ══════════════════════════════════════════════════════════
 *  1. CREATE ACCOUNT
 * ══════════════════════════════════════════════════════════ */

/*
 * create_account – prompts the user for a slot number, personal
 * details, opening balance, and a 4-digit PIN.  Enforces the
 * minimum-balance policy before writing to disk.
 *
 * Returns: SUCCESS or an ERR_* code.
 */
int create_account(FILE *fp)
{
    print_separator('=', 50);
    printf("  CREATE NEW ACCOUNT\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Account number (1-100): ");

    ClientData c;
    if (read_record(fp, slot, &c) == SUCCESS && c.is_active) {
        printf("  [!] Account #%u already exists.\n", slot);
        return ERR_EXISTS;
    }

    /* Gather personal info ────────────────────────────────── */
    memset(&c, 0, sizeof(c));
    c.acct_num = slot;
    get_string("  Last name  : ", c.last_name,  sizeof(c.last_name));
    get_string("  First name : ", c.first_name, sizeof(c.first_name));

    c.balance = get_positive_double(
        "  Opening balance (min ₹500.00): ");

    if (c.balance < MIN_BALANCE) {
        printf("  [!] Opening balance must be at least ₹%.2f.\n",
               MIN_BALANCE);
        return ERR_MIN_BALANCE;
    }

    /* 4-digit PIN ─────────────────────────────────────────── */
    char raw_pin[MAX_PIN_LEN];
    get_pin("  Set 4-digit PIN: ", raw_pin);

    char hashed[16];
    /* Re-use the same djb2-based hash from utils.c via a
       local helper – we avoid exposing simple_hash directly. */
    unsigned int h = 5381u;
    for (const char *p = raw_pin; *p; p++)
        h = ((h << 5) + h) ^ (unsigned char)*p;
    snprintf(hashed, sizeof(hashed), "%u", h);
    strncpy(c.pin, hashed, sizeof(c.pin) - 1);

    c.is_active       = 1;
    c.daily_withdrawn = 0.0;

    /* Get today for the daily-limit reset logic */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(c.last_txn_date, sizeof(c.last_txn_date), "%Y-%m-%d", t);

    /* Write to file ────────────────────────────────────────── */
    if (write_record(fp, slot, &c) != SUCCESS) {
        printf("  [!] Failed to write record.\n");
        return ERR_FILE;
    }

    log_transaction(slot, TXN_CREATE, c.balance, c.balance,
                    "Account created");

    printf("\n  [OK] Account #%u created for %s %s. Balance: ₹%.2f\n",
           slot, c.first_name, c.last_name, c.balance);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  2. DISPLAY ALL ACCOUNTS
 * ══════════════════════════════════════════════════════════ */

/*
 * display_all_accounts – scans every slot sequentially and
 * prints a formatted table of active accounts.
 * Returns the count of active accounts found.
 */
int display_all_accounts(FILE *fp)
{
    print_separator('=', 50);
    printf("  ALL ACTIVE ACCOUNTS\n");
    print_separator('=', 50);

    rewind(fp);
    print_account_header();

    int count = 0;
    double total = 0.0;
    ClientData c;

    for (unsigned int slot = 1; slot <= MAX_ACCOUNTS; slot++) {
        if (read_record(fp, slot, &c) == SUCCESS && c.is_active) {
            print_account_row(&c);
            total += c.balance;
            count++;
        }
    }

    print_separator('-', 68);
    printf("  Total accounts: %-3d     Total deposits: Rs %.2f\n",
           count, total);
    print_separator('-', 68);

    if (count == 0)
        printf("  [i] No active accounts found.\n");

    return count;
}

/* ══════════════════════════════════════════════════════════
 *  3. SEARCH ACCOUNT
 * ══════════════════════════════════════════════════════════ */

/*
 * search_account – looks up an account by number and displays
 * full details if found.
 */
int search_account(FILE *fp)
{
    print_separator('=', 50);
    printf("  SEARCH ACCOUNT\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Enter account number: ");

    ClientData c;
    if (read_record(fp, slot, &c) != SUCCESS || !c.is_active) {
        printf("  [!] Account #%u not found.\n", slot);
        return ERR_NOT_FOUND;
    }

    print_separator('-', 50);
    printf("  Account Number : %u\n",   c.acct_num);
    printf("  Account Holder : %s %s\n", c.first_name, c.last_name);
    printf("  Balance        : ₹%.2f\n", c.balance);
    printf("  Status         : %s\n",   c.is_active ? "ACTIVE" : "CLOSED");
    printf("  Daily Withdrawn: ₹%.2f / ₹%.2f\n",
           c.daily_withdrawn, ATM_DAILY_LIMIT);
    print_separator('-', 50);

    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  4. UPDATE ACCOUNT BALANCE (manual charge / payment)
 * ══════════════════════════════════════════════════════════ */

/*
 * update_account_balance – mirrors the original program's
 * update feature: enter a positive charge or negative payment.
 */
int update_account_balance(FILE *fp)
{
    print_separator('=', 50);
    printf("  UPDATE ACCOUNT BALANCE\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Enter account number: ");

    ClientData c;
    if (read_record(fp, slot, &c) != SUCCESS || !c.is_active) {
        printf("  [!] Account #%u not found.\n", slot);
        return ERR_NOT_FOUND;
    }

    printf("\n  Current record:\n");
    print_account_header();
    print_account_row(&c);

    printf("\n  Enter amount (+charge / -payment): ");
    double txn;
    char buf[32];
    fgets(buf, sizeof(buf), stdin);
    if (sscanf(buf, "%lf", &txn) != 1) {
        printf("  [!] Invalid amount.\n");
        return ERR_INVALID;
    }

    double new_bal = c.balance + txn;
    if (new_bal < MIN_BALANCE) {
        printf("  [!] Transaction would breach minimum balance "
               "(₹%.2f).\n", MIN_BALANCE);
        return ERR_MIN_BALANCE;
    }

    c.balance = new_bal;
    write_record(fp, slot, &c);
    log_transaction(slot, txn >= 0 ? TXN_DEPOSIT : TXN_WITHDRAW,
                    txn, c.balance, "Manual adjustment");

    printf("\n  [OK] Updated balance: ₹%.2f\n", c.balance);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  5. DELETE ACCOUNT
 * ══════════════════════════════════════════════════════════ */

/*
 * delete_account – marks a record inactive (soft delete) and
 * zeroes balance.  Requires PIN confirmation.
 */
int delete_account(FILE *fp)
{
    print_separator('=', 50);
    printf("  DELETE ACCOUNT\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Enter account number to delete: ");

    ClientData c;
    if (read_record(fp, slot, &c) != SUCCESS || !c.is_active) {
        printf("  [!] Account #%u does not exist.\n", slot);
        return ERR_NOT_FOUND;
    }

    printf("  [WARNING] You are about to delete account #%u "
           "(%s %s, ₹%.2f).\n",
           slot, c.first_name, c.last_name, c.balance);

    char confirm[4];
    get_string("  Type YES to confirm: ", confirm, sizeof(confirm));
    if (strcmp(confirm, "YES") != 0) {
        printf("  [i] Deletion cancelled.\n");
        return ERR_INVALID;
    }

    if (!verify_account_pin(fp, slot)) {
        printf("  [!] PIN verification failed. Deletion aborted.\n");
        return ERR_AUTH;
    }

    log_transaction(slot, TXN_DELETE, c.balance, 0.0,
                    "Account deleted");

    memset(&c, 0, sizeof(c));          /* zero out the record   */
    write_record(fp, slot, &c);

    printf("  [OK] Account #%u has been permanently deleted.\n", slot);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  6. DEPOSIT
 * ══════════════════════════════════════════════════════════ */

int deposit(FILE *fp)
{
    print_separator('=', 50);
    printf("  DEPOSIT FUNDS\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Enter account number: ");

    ClientData c;
    if (read_record(fp, slot, &c) != SUCCESS || !c.is_active) {
        printf("  [!] Account #%u not found.\n", slot);
        return ERR_NOT_FOUND;
    }

    double amount = get_positive_double("  Deposit amount: ₹");
    c.balance += amount;
    write_record(fp, slot, &c);
    log_transaction(slot, TXN_DEPOSIT, amount, c.balance, "Cash deposit");

    printf("\n  [OK] ₹%.2f deposited.\n"
           "       New balance: ₹%.2f\n", amount, c.balance);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  7. ATM WITHDRAWAL
 * ══════════════════════════════════════════════════════════ */

/*
 * atm_withdraw – enforces minimum balance, daily ATM limit,
 * and PIN verification before debiting the account.
 */
int atm_withdraw(FILE *fp)
{
    print_separator('=', 50);
    printf("  ATM WITHDRAWAL\n");
    print_separator('=', 50);

    unsigned int slot = (unsigned int)
        get_valid_account_num("Enter account number: ");

    ClientData c;
    if (read_record(fp, slot, &c) != SUCCESS || !c.is_active) {
        printf("  [!] Account #%u not found.\n", slot);
        return ERR_NOT_FOUND;
    }

    /* PIN check ────────────────────────────────────────────── */
    if (!verify_account_pin(fp, slot)) {
        printf("  [!] Incorrect PIN. Transaction aborted.\n");
        return ERR_AUTH;
    }

    reset_daily_limit_if_needed(&c);

    double amount = get_positive_double("  Withdrawal amount: ₹");

    /* Daily limit check ───────────────────────────────────── */
    if (c.daily_withdrawn + amount > ATM_DAILY_LIMIT) {
        printf("  [!] Exceeds daily ATM limit of ₹%.2f.\n"
               "      Already withdrawn today: ₹%.2f\n",
               ATM_DAILY_LIMIT, c.daily_withdrawn);
        return ERR_LIMIT;
    }

    /* Minimum balance check ───────────────────────────────── */
    if (c.balance - amount < MIN_BALANCE) {
        printf("  [!] Insufficient funds. Minimum balance ₹%.2f "
               "must be maintained.\n"
               "      Current balance: ₹%.2f\n",
               MIN_BALANCE, c.balance);
        return ERR_MIN_BALANCE;
    }

    c.balance          -= amount;
    c.daily_withdrawn  += amount;
    write_record(fp, slot, &c);
    log_transaction(slot, TXN_WITHDRAW, amount, c.balance,
                    "ATM withdrawal");

    printf("\n  [OK] ₹%.2f dispensed.\n"
           "       Remaining balance : ₹%.2f\n"
           "       Daily limit used  : ₹%.2f / ₹%.2f\n",
           amount, c.balance, c.daily_withdrawn, ATM_DAILY_LIMIT);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  8. TRANSFER FUNDS
 * ══════════════════════════════════════════════════════════ */

/*
 * transfer_funds – debits one account and credits another in a
 * single operation.  Minimum-balance policy is enforced on the
 * source account.  Both account numbers must differ.
 */
int transfer_funds(FILE *fp)
{
    print_separator('=', 50);
    printf("  TRANSFER FUNDS\n");
    print_separator('=', 50);

    unsigned int from = (unsigned int)
        get_valid_account_num("From account number: ");
    unsigned int to   = (unsigned int)
        get_valid_account_num("To   account number: ");

    if (from == to) {
        printf("  [!] Source and destination must differ.\n");
        return ERR_INVALID;
    }

    ClientData src, dst;
    if (read_record(fp, from, &src) != SUCCESS || !src.is_active) {
        printf("  [!] Source account #%u not found.\n", from);
        return ERR_NOT_FOUND;
    }
    if (read_record(fp, to, &dst) != SUCCESS || !dst.is_active) {
        printf("  [!] Destination account #%u not found.\n", to);
        return ERR_NOT_FOUND;
    }

    if (!verify_account_pin(fp, from)) {
        printf("  [!] Source PIN invalid. Transfer aborted.\n");
        return ERR_AUTH;
    }

    double amount = get_positive_double("  Transfer amount: ₹");

    if (src.balance - amount < MIN_BALANCE) {
        printf("  [!] Insufficient funds after minimum balance "
               "requirement.\n");
        return ERR_MIN_BALANCE;
    }

    src.balance -= amount;
    dst.balance += amount;
    write_record(fp, from, &src);
    write_record(fp, to,   &dst);

    char desc[40];
    snprintf(desc, sizeof(desc), "Transfer to #%u", to);
    log_transaction(from, TXN_TRANSFER, amount, src.balance, desc);
    snprintf(desc, sizeof(desc), "Transfer from #%u", from);
    log_transaction(to,   TXN_TRANSFER, amount, dst.balance, desc);

    printf("\n  [OK] ₹%.2f transferred from #%u to #%u.\n",
           amount, from, to);
    printf("       #%u new balance: ₹%.2f\n", from, src.balance);
    printf("       #%u new balance: ₹%.2f\n", to,   dst.balance);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  9. EXPORT TEXT REPORT
 * ══════════════════════════════════════════════════════════ */

/*
 * export_text_report – writes a human-readable accounts.txt,
 * equivalent to the original textFile() but using the improved
 * ClientData structure.
 */
int export_text_report(FILE *fp)
{
    FILE *out = fopen(ACCOUNTS_TXT, "w");
    if (!out) { perror("export_text_report"); return ERR_FILE; }

    rewind(fp);
    fprintf(out, "%-6s %-15s %-10s %12s  %s\n",
            "Acct", "Last Name", "First Name", "Balance", "Status");
    fprintf(out, "%s\n",
            "----------------------------------------------------------"
            "----------");

    int count = 0;
    ClientData c;
    for (unsigned int slot = 1; slot <= MAX_ACCOUNTS; slot++) {
        if (read_record(fp, slot, &c) == SUCCESS && c.is_active) {
            fprintf(out, "%-6u %-15s %-10s %12.2f  ACTIVE\n",
                    c.acct_num, c.last_name, c.first_name, c.balance);
            count++;
        }
    }

    fclose(out);
    printf("  [OK] %d account(s) exported to '%s'.\n",
           count, ACCOUNTS_TXT);
    return SUCCESS;
}

/* ══════════════════════════════════════════════════════════
 *  10. VIEW TRANSACTION HISTORY
 * ══════════════════════════════════════════════════════════ */

/*
 * view_transaction_history – reads the binary log and prints
 * all entries for the given account number.  Pass 0 to see all
 * transactions (admin view).
 */
int view_transaction_history(unsigned int acct_num)
{
    FILE *fp = fopen(TRANS_FILE, "rb");
    if (!fp) {
        printf("  [i] No transaction history found.\n");
        return ERR_FILE;
    }

    print_separator('=', 70);
    if (acct_num == 0)
        printf("  ALL TRANSACTIONS\n");
    else
        printf("  TRANSACTION HISTORY – Account #%u\n", acct_num);
    print_separator('=', 70);
    printf("  %-6s %-4s %12s %12s  %-19s  %s\n",
           "Acct", "Type", "Amount", "Balance", "Timestamp",
           "Description");
    print_separator('-', 70);

    Transaction t;
    int count = 0;
    char type_str[12];

    while (fread(&t, sizeof(Transaction), 1, fp) == 1) {
        if (acct_num != 0 && t.acct_num != acct_num) continue;

        switch (t.type) {
            case TXN_DEPOSIT:  strcpy(type_str, "DEP");  break;
            case TXN_WITHDRAW: strcpy(type_str, "WDR");  break;
            case TXN_TRANSFER: strcpy(type_str, "TRF");  break;
            case TXN_CREATE:   strcpy(type_str, "NEW");  break;
            case TXN_DELETE:   strcpy(type_str, "DEL");  break;
            default:           strcpy(type_str, "???");  break;
        }

        printf("  %-6u %-4s %12.2f %12.2f  %-19s  %s\n",
               t.acct_num, type_str, t.amount, t.balance_after,
               t.timestamp, t.description);
        count++;
    }

    fclose(fp);
    print_separator('-', 70);
    printf("  %d record(s) shown.\n", count);
    return count;
}

/* ══════════════════════════════════════════════════════════
 *  11 & 12. SORTING  (in-memory, using qsort)
 * ══════════════════════════════════════════════════════════ */

static ClientData g_sort_buf[MAX_ACCOUNTS]; /* scratch space     */
static int        g_sort_count = 0;

/* Comparators for qsort ───────────────────────────────────── */
static int cmp_balance_desc(const void *a, const void *b)
{
    const ClientData *ca = (const ClientData *)a;
    const ClientData *cb = (const ClientData *)b;
    if (cb->balance > ca->balance) return  1;
    if (cb->balance < ca->balance) return -1;
    return 0;
}

static int cmp_name_asc(const void *a, const void *b)
{
    const ClientData *ca = (const ClientData *)a;
    const ClientData *cb = (const ClientData *)b;
    int r = strcmp(ca->last_name, cb->last_name);
    if (r != 0) return r;
    return strcmp(ca->first_name, cb->first_name);
}

/* Helper – load active accounts into g_sort_buf ─────────── */
static void load_active(FILE *fp)
{
    g_sort_count = 0;
    ClientData c;
    for (unsigned int s = 1; s <= MAX_ACCOUNTS; s++) {
        if (read_record(fp, s, &c) == SUCCESS && c.is_active) {
            g_sort_buf[g_sort_count++] = c;
            if (g_sort_count == MAX_ACCOUNTS) break;
        }
    }
}

/* Helper – display the sorted buffer ────────────────────── */
static void display_sorted(void)
{
    print_account_header();
    for (int i = 0; i < g_sort_count; i++)
        print_account_row(&g_sort_buf[i]);
    print_separator('-', 68);
    printf("  %d account(s) listed.\n", g_sort_count);
}

/*
 * sort_accounts_by_balance – highest balance first.
 */
void sort_accounts_by_balance(FILE *fp)
{
    print_separator('=', 50);
    printf("  ACCOUNTS SORTED BY BALANCE (descending)\n");
    print_separator('=', 50);

    load_active(fp);
    qsort(g_sort_buf, (size_t)g_sort_count,
          sizeof(ClientData), cmp_balance_desc);
    display_sorted();
}

/*
 * sort_accounts_by_name – alphabetical by last name, then first.
 */
void sort_accounts_by_name(FILE *fp)
{
    print_separator('=', 50);
    printf("  ACCOUNTS SORTED BY NAME (A-Z)\n");
    print_separator('=', 50);

    load_active(fp);
    qsort(g_sort_buf, (size_t)g_sort_count,
          sizeof(ClientData), cmp_name_asc);
    display_sorted();
}
/*
 * ============================================================
 *  BANKING MANAGEMENT SYSTEM - main.c
 *  Entry point: admin authentication → main menu loop.
 *
 *  Build:
 *    gcc -Wall -Wextra -o banking main.c utils.c operations.c
 *
 *  Run:
 *    ./banking
 * ============================================================
 */


int main(void)
{
    /* ── System initialisation ──────────────────────────── */
    init_database();         /* create credit.dat if missing */

    /* ── Open the random-access database ─────────────────── */
    FILE *db = fopen(DB_FILE, "rb+");
    if (!db) {
        perror("Cannot open database");
        return EXIT_FAILURE;
    }

    /* ── Main menu loop ──────────────────────────────────── */
    unsigned int choice;

    do {
        print_banner();
        print_menu();
        choice = get_menu_choice(0, 12);

        switch (choice) {
        case 1:
            create_account(db);
            break;
        case 2:
            display_all_accounts(db);
            break;
        case 3:
            search_account(db);
            break;
        case 4:
            update_account_balance(db);
            break;
        case 5:
            delete_account(db);
            break;
        case 6:
            deposit(db);
            break;
        case 7:
            atm_withdraw(db);
            break;
        case 8:
            transfer_funds(db);
            break;
        case 9:
            export_text_report(db);
            break;
        case 10: {
            /* Ask whether to view one account or all */
            printf("  Enter account number (0 = all accounts): ");
            int acct = 0;
            char buf[16];
            fgets(buf, sizeof(buf), stdin);
            sscanf(buf, "%d", &acct);
            view_transaction_history((unsigned int)acct);
            break;
        }
        case 11:
            sort_accounts_by_balance(db);
            break;
        case 12:
            sort_accounts_by_name(db);
            break;
        case 0:
            printf("\n  Thank you for using the Banking System. Goodbye!\n");
            break;
        default:
            printf("  [!] Invalid option.\n");
            break;
        }

        if (choice != 0)
            pause_prompt();

    } while (choice != 0);

    fclose(db);
    return EXIT_SUCCESS;
}