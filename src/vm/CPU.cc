#include "CPU.hh"
#include <iostream>
#include <fstream>
#include <cmath>
#include <stdexcept>

#define WINDOWS __CYGWIN__ || _WIN32

#if WINDOWS

#include <Windows.h>
#include <conio.h>

bool
Memory::checkSTDIN()
{
	HANDLE hStdin = GetStdHandle( STD_INPUT_HANDLE );
	return WaitForSingleObject( hStdin, 1000 ) == WAIT_OBJECT_0 && _kbhit();
}

#else

#include <unistd.h>

bool
Memory::checkSTDIN()
{
	fd_set readfds;
	FD_ZERO( &readfds );
	FD_SET( STDIN_FILENO, &readfds );

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select( 1, &readfds, NULL, NULL, &timeout ) != 0;
}

#endif

enum
{
	P = 1,
	Z = 2,
	N = 4,
	KBSR = 0xFE00,
	KBDR = 0xFE02,
	PC_START = 0x3000,
	PSR_START = 0x700,
};

enum Opcode
{
	BR,
	ADD,
	LD,
	ST,
	JSR,
	AND,
	LDR,
	STR,
	RTI,
	NOT,
	LDI,
	STI,
	JMP,
	RES,
	LEA,
	TRAP
};

enum Traps
{
	GETC = 0x20,
	OUT,
	PUTS,
	IN,
	PUTSP,
	HALT
};

unsigned short&
Memory::operator[]( const unsigned short addr )
{
	if ( addr == KBSR )
	{
		if ( checkSTDIN() )
		{
			mem[addr] = 0x8000;
			mem[KBDR] = getchar();
		}
		else
		{
			mem[addr] = 0;
		}
	}
	return mem[addr];
}


CPU::CPU() : halted( false ), PC( PC_START ), PSR( PSR_START ) { }

void
CPU::loadPrograms( int argc, char* argv[] )
{
	for ( int i = 1; i < argc; i++ )
	{
		loadProgram( argv[i] );	
	}
}

unsigned short
CPU::toLittleEndian( const unsigned short val )
{
	return ( val << 8 ) | ( val >> 8 );
}

void
CPU::loadProgram( const char* filePath )
{
	std::ifstream f;
	f.open( filePath, std::ios::binary );
	
	if ( !f.is_open() )
	{
		throw std::runtime_error( "Can not open: " + std::string( filePath ) );
	}

	f.seekg (0, f.end);
	int length = f.tellg();
	f.seekg (0, f.beg);

	if ( length > MEM_SIZE * 2 )
	{
		throw std::runtime_error( std::string( filePath ) + " is too large" );
	}  

	unsigned short origin[1];

	f.read( reinterpret_cast<char*>( origin ), 2 );
	origin[0] = toLittleEndian( origin[0] );
	unsigned short* p = &mem[0] + origin[0];
	
	f.read( reinterpret_cast<char*>( p ), length - 2); 
	f.close();

	for ( int i = 0; i < round( ( length - 2 ) / 2.0 ); i++ )
	{
		*p = toLittleEndian( *p );
		p++;		
	}
}

bool
CPU::isHalted()
{
	return halted;
}

void
CPU::setcc( const unsigned short val )
{
	PSR &= 0xFFF8;
	if ( val & 0x8000 )
	{
		PSR |= N;
	}
	else if ( val == 0 )
	{
		PSR |= Z;
	}
	else
	{
		PSR |= P;
	}
}

unsigned short
CPU::sext( unsigned short val, const int len )
{
	if ( ( val >> ( len - 1 ) ) & 0x1 )
	{
		val |= 0xFFFF << len;
	}
	return val;
}

unsigned short
CPU::fetchInstr()
{
	return mem[PC++];
}

void
CPU::handleTrap( const unsigned short instr )
{
	GPR[7] = PC;
	switch ( instr & 0xFF )
	{
		case GETC:
		{
			GPR[0] = static_cast<unsigned short>( getchar() );
			setcc( GPR[0] );
			break;
		}
		case OUT:
		{
			std::cout << static_cast<char>( GPR[0] );
			std::cout.flush();
			break;
		}
		case PUTS:
		{
			unsigned short* c = &mem[0] + GPR[0];
			while ( *c )
			{
				std::cout << static_cast<char>( *c );
				c++;
			} 
			std::cout.flush();
			break;
		}
		case IN:
		{
			std::cout << "Enter a character: ";
			char c = getchar();
			std::cout << c;
			std::cout.flush();
			GPR[0] = static_cast<unsigned short>( c );
			setcc( GPR[0] );
			break;
		}
		case PUTSP:
		{
			unsigned short* c = &mem[0] + GPR[0];
			while ( *c )
			{
				char c1 = static_cast<char>( *c );
				std::cout << c1;
				char c2 = static_cast<char>( *c >> 8 );
				if ( c2 )
				{
					std::cout << c2;
				}
				c++;
			}
			std::cout.flush();
			break;
		}
		case HALT:
		{
			halted = true;
			break;
		}
	}
}

void
CPU::halt()
{
	halted = true;
}

void
CPU::handleInstr( const unsigned short instr )
{
	unsigned short r0, r1, r2, imm, imm5, offBase, offPC;
	const unsigned short opcode = instr >> 12;
	const unsigned short opbit = 1 << opcode;

	if ( opbit & 0x4EEF )
	{
		r0 = ( instr >> 9 ) & 0x7; 
	} 
	if ( opbit & 0x12F3 )
	{
		r1 = ( instr >> 6 ) & 0x7;
	}
	if ( opbit & 0x22 )
	{
		imm = instr & 0x20;
		if ( imm )
		{
			imm5 = sext( instr & 0x1F, 5 );
		}
		else
		{
			r2 = instr & 0x7;
		}
	}
	if ( opbit & 0xC0 )
	{
		offBase = GPR[r1] + sext( instr & 0x3F, 6 );
	}
	else if ( opbit & 0x4C0D )
	{
		offPC = PC + sext( instr & 0x1FF, 9 );
	} 

	switch ( opcode )
	{
		case BR:
		{
			PC = ( PSR & 0x7 ) & r0 ? offPC : PC;
			break;
		}
		case ADD:
		{
			GPR[r0] = imm ? GPR[r1] + imm5 : GPR[r1] + GPR[r2];
			break;
		}
		case LD:
		{
			GPR[r0] = mem[offPC];
			break;
		}
		case ST:
		{
			mem[offPC] = GPR[r0];
			break;
		}
		case JSR:
		{
			GPR[7] = PC;
			PC = instr & 0x800 ? PC + sext( instr & 0x7FF, 11 ) : GPR[r1];
			break;
		}
		case AND:
		{
			GPR[r0] = imm ? GPR[r1] & imm5 : GPR[r1] & GPR[r2];
			break;
		}
		case LDR:
		{
			GPR[r0] = mem[offBase];
			break;
		}
		case STR:
		{
			mem[offBase] = GPR[r0];
			break;
		}
		case RTI:
		{
			if ( !( PSR & 0x8000 ) )
			{
				PC = mem[GPR[6]++];
				PSR = mem[GPR[6]++];
				break;
			}
			else
			{
				throw std::runtime_error( "Privilege Mode Violation" );
			}
		}
		case NOT:
		{
			GPR[r0] = ~GPR[r1];
			break;
		}
		case LDI:
		{
			GPR[r0] = mem[mem[offPC]];
			break;
		}
		case STI:
		{
			mem[mem[offPC]] = GPR[r0];
			break;
		}
		case JMP:
		{
			PC = GPR[r1];
			break;
		}
		case LEA:
		{
			GPR[r0] = offPC;
			break;
		}
		case TRAP:
		{
			handleTrap( instr ); 
			break;
		}
		default:
		{
			throw std::runtime_error( "Invalid Opcode: " + std::to_string( opcode ) );
		}
	}
	
	if ( opbit & 0x4666 )
	{
		setcc( GPR[r0] );
	}
}
