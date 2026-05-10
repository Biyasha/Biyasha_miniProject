# Banking Management System
### CS3251 Mini Project

***

## Project Structure

```text
Project Folder/
├── credit.dat        # Binary database file
├── accounts.txt      # Exported account details
├── transactions.txt  # Transaction history
├── trans1.c          # Main source code
└── README.md         # Documentation
```

## Features

- Create new account
- Display all accounts
- Search account
- Update balance
- Delete account
- Deposit money
- ATM withdrawal
- Fund transfer
- Transaction history
- Export account details
- Sorting accounts
- Input validation
- PIN protection

## Technologies Used

- C Programming Language
- Structures
- Functions
- File Handling
- Random Access Files

## How to Compile

```bash
gcc trans1.c -o banking
```

## How to Run

```bash
./banking
```

## Files Description

| File Name | Description |
|----------|-------------|
| `trans1.c` | Main program source code |
| `credit.dat` | Stores account records |
| `accounts.txt` | Exported account report |
| `transactions.txt` | Stores transaction logs |
| `README.md` | Project documentation |

## Key Functionalities

### Account Management

- Create account
- Search account
- Delete account
- Display all accounts

### Banking Operations

- Deposit money
- Withdraw money
- Fund transfer

### Security Features

- PIN verification
- Minimum balance checking
- Input validation

## Future Improvements

- GUI interface
- Database integration
- Online banking support
- Encryption for account data

## Conclusion

This project demonstrates the implementation of a Banking Management System using C programming with file handling and structured programming concepts.