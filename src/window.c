#include <sys/ioctl.h>

int get_window_size(int *rows, int *cols)
{
	struct winsize size;
	int ioctl_result = ioctl(0, TIOCGWINSZ, &size);
	if (ioctl_result == -1 || size.ws_col == 0) {
		return -1;
	}
	else {
		*rows = size.ws_row;
		*cols = size.ws_col;
		return 0;
	}
}

