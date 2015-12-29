/* This is a simple TCP client that connects to port 1234 and prints a list
 * of files in a given directory.
 *
 * It directly deserializes and serializes messages from network, minimizing
 * memory use.
 * 
 * For flexibility, this example is implemented using posix api.
 * In a real embedded system you would typically use some other kind of
 * a communication and filesystem layer.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "fileproto.pb.h"
#include "common.h"

/* This callback function will be called once for each filename received
 * from the server. The filenames will be printed out immediately, so that
 * no memory has to be allocated for them.
 */
bool printfile_callback(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    FileInfo fileinfo = {};
    
    if (!pb_decode(stream, FileInfo_fields, &fileinfo))
        return false;
    
    printf("%d %s\n", fileinfo.type, fileinfo.name);
    
    return true;
}

/* This function sends a request to socket 'fd' to list the files in
 * directory given in 'path'. The results received from server will
 * be printed to stdout.
 */
bool listdir(FILE* f, char *path)
{
    /* Construct and send the request to server */
    {
        ListFilesRequest request = {};
        pb_ostream_t output = pb_ostream_from_file(f);
        uint8_t zero = 0;
        
        /* In our protocol, path is optional. If it is not given,
         * the server will list the root directory. */

		if (strlen(path) + 1 > sizeof(request.path))
		{
			fprintf(stderr, "Too long path.\n");
			return false;
		}

		strcpy(request.path, path);

        
        /* Encode the request. It is written to the socket immediately
         * through our custom stream. */
        if (!pb_encode(&output, ListFilesRequest_fields, &request))
        {
            fprintf(stderr, "Encoding failed: %s\n", PB_GET_ERROR(&output));
            return false;
        }
        
        /* We signal the end of request with a 0 tag. */
        pb_write(&output, &zero, 1);
    }
    
    /* Read back the response from server */
    {
        ListFilesResponse response = {};
        pb_istream_t input = pb_istream_from_file(f);
        
        /* Give a pointer to our callback function, which will handle the
         * filenames as they arrive. */
        response.file.funcs.decode = &printfile_callback;
        
        if (!pb_decode(&input, ListFilesResponse_fields, &response))
        {
            fprintf(stderr, "Decode failed: %s\n", PB_GET_ERROR(&input));
            return false;
        }
        
        /* If the message from server decodes properly, but directory was
         * not found on server side, we get path_error == true. */
        if (response.path_error)
        {
            fprintf(stderr, "Server reported error.\n");
            return false;
        }
    }
    
    return true;
}

int main(int argc, char **argv)
{
    FILE* f;
    char *path = NULL;
    char *dev = NULL;
    
    if (argc > 2)
    {
        path = argv[1];
        dev = argv[2];
    }
    
    f = fopen(dev, "w+");

    if (!f)
    {
        perror("dev open");
        return 1;
    }
    
    /* Send the directory listing request */
    if (!listdir(f, path))
        return 2;
    
    /* Close connection */
    fclose(f);
    
    return 0;
}
