\newpage

# S3

## Prerequisites

First of all, you need to have an AWS account, an IAM user and S3 bucket.

To create an IAM user:

1. Sign in to the AWS Management Console and open the [IAM console](https://console.aws.amazon.com/iam/).

2. In the navigation pane, choose Users and then choose Add users.

3. Type the user name for the new user.

4. Select the type of access to be both programmatic access and access to the AWS Management Console.

5. Choose Next: Permissions.

6. On the Set permissions page, select attach existing policies directly, search for AmazonS3FullAccess and choose it, then choose Next: Review, then choose Add permissions.

7. Choose Next: Tags.

8. Choose Next: Review to see all of the choices you made up to this point. When you are ready to proceed, choose Create user.

9. To view the users' access keys (access key IDs and secret access keys), choose Show next to each password and access key that you want to see. To save the access keys, choose Download .csv and then save the file to a safe location.

You are now ready to create a S3 bucket, To create a S3 bucket:

1. Sign in to the AWS Management Console using your IAM user credentials and open the [Amazon S3 console](https://console.aws.amazon.com/s3/).

2. Choose Create bucket.

3. In Bucket name, enter a name for your bucket.

4. In Region, choose the AWS Region where you want the bucket to reside. 

5. Keep the default values as it is and Choose Create bucket.

## Modify the pgmoneta configuration

You need to have a storage space for 1 backup on your local computer.

Change `pgmoneta.conf` to add

``` ini
storage_engine = s3
s3_aws_region = the-aws-region
s3_access_key_id = your-access-key-id-from-the-downloaded-file
s3_secret_access_key = your-secret-access-key-from-the-downloaded-file
s3_bucket = your-s3-bucket-name
s3_base_dir = directory-where-backups-will-be-stored-in
```

under the `[pgmoneta]` section.
