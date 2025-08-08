CC = g++
CPPFLAGS = -static-libgcc -static-libstdc++ -W -Wall -g

OPT = -mwindows -municode
LDLIBS = -lcomctl32 -lws2_32

HDRDIR	= .
SRCDIR	= .
RSRCDIR	= .
OBJDIR	= .
EXEDIR	= .

HDRS = $(notdir $(wildcard $(HDRDIR)/*.h))
SRCS_EXTRA = $(notdir $(wildcard $(SRCDIR)/*.cpp))
SRCS = $(filter-out main.cpp, $(SRCS_EXTRA))
OBJS = $(patsubst %.cpp, %.o, $(SRCS))
RSRCS = $(notdir $(wildcard $(RSRCDIR)/*.rc))

OUT_OBJS = $(addprefix $(OBJDIR)/, $(OBJS))
OUT_RSRCS = $(addprefix $(RSRCDIR)/, $(RSRCS))
OUT_RES = $(join $(OBJDIR)/, resource.res)

MAIN = $(SRCDIR)/main.cpp
EXEC = ChatHub
TARGET = $(EXEDIR)/$(EXEC).exe

$(TARGET): $(MAIN) $(OUT_OBJS) $(OUT_RES)
	$(CC) $(CPPFLAGS) -o $@ $^ $(OPT) $(LDHDR) $(LDLIBS)

vpath %.cpp $(SRCDIR)
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) -c $< -o $@ -municode

$(OUT_RES): $(OUT_RSRCS)
	windres $(OUT_RSRCS) -O coff -o $@ -I$(HDRDIR)

pr:
	@echo "hdr list = $(HDRS)"
	@echo "res list = $(RES)"
	@echo "src list = $(SRCS)"
	@echo "rsrc list = $(RSRCS)"
	@echo "objs list = $(OBJS)"
	@echo "OUT_OBJS list = $(OUT_OBJS)"
	@echo "OUT_RSRCS list = $(OUT_RSRCS)"
	@echo "OUT_RES list = $(OUT_RES)"
	@echo "TRCF = $(TRCF)"

