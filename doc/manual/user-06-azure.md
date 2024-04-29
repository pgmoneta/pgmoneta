\newpage

# Azure

## Prerequisites

First of all, you need to have an Azure account, an Azure storage account and a blob container.

A container organizes a set of blobs, similar to a directory in a file system. A storage account can include an unlimited number of containers, and a container can store an unlimited number of blobs.

To create an Azure storage account with the Azure portal:

1. Sign in to the [Azure portal][azure].

2. From the left portal menu, select Storage accounts to display a list of your storage accounts. If the portal menu isn't visible, click the menu button to toggle it on.

3. On the Storage accounts page, select Create.

4. On the Basics tab, provide a resource group name and storage account name. You can go for the default settings of the other fields.

5. Choose Next: Advanced.

6. On the Advanced tab, you can configure additional options and modify default settings for your new storage account. You can go for the default settings.

7. Choose Next: Networking.

8. On the Networking tab, you can go for the default settings.

9. Choose Next: Data protection.

10. On the Data protection tab, you can for the default settings.

11. Choose Next: Encryption.

12. On the Encryption tab, you can for the default settings.

13. Choose Next: Tags.

14. Choose Next: Review to see all of the choices you made up to this point. When you are ready to proceed, choose Create.

To create a blob container with the Azure portal:

1. In the navigation pane for the storage account, scroll to the `Data storage` section and select Containers.

2. Within the Containers pane, select the `+ Container` button to open the New container pane.

3. Within the New Container pane, provide a Name for your new container.

4. Select Create to create the container.

To get the Azure storage account shared key which is required for pgmoneta configuration:

1. In the navigation pane for the storage account, scroll to the `Security + networking` section and select Access Keys.

2. Under key1, find the Key value. Select the Copy button to copy the account key.

You can use either of the two keys to access Azure Storage, but in general it's a good practice to use the first key, and reserve the use of the second key for when you are rotating keys.

## Modify the pgmoneta configuration

You need to have a storage space for 1 backup on your local computer.

Change `pgmoneta.conf` to add

``` ini
storage_engine = azure
azure_storage_account = the-storage-account-name
azure_container = the-container-name
azure_shared_key = the-storage-account-shared-key
azure_base_dir = directory-where-backups-will-be-stored-in
```

under the `[pgmoneta]` section.
